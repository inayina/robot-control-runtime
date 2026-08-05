// Daemon 组合层：拥有 rcrd 生命周期并按逆序回收 Core、线程和 Linux fd。
#include "rcr/runtime_daemon.hpp"

#include "rcr/time.hpp"
#include "rcr/vcan.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include <sched.h>

namespace rcr {
namespace {

void log_line(std::string_view level, std::string_view message) {
  // 结构化程度保持最低：级别 + 消息。周期路径禁止调用本函数。
  std::cerr << "rcrd level=" << level << " msg=" << message << '\n';
}

} // namespace

RuntimeDaemon::RuntimeDaemon(DaemonConfig config)
    : config_(std::move(config)) {}

RuntimeDaemon::~RuntimeDaemon() { stop(); }

bool RuntimeDaemon::started() const noexcept {
  return started_.load(std::memory_order_acquire);
}

DaemonExitCode RuntimeDaemon::exit_code() const noexcept {
  return exit_code_.load(std::memory_order_acquire);
}

void RuntimeDaemon::rollback_started_parts() {
  if (io_) {
    io_->request_stop();
    io_->join();
    io_.reset();
  }
  if (runtime_) {
    runtime_->stop();
  }
  supervisor_.reset();
  runtime_.reset();
  queue_.reset();
  stop_event_ = EventFd{};
  signals_ = SignalFd{};
  started_.store(false, std::memory_order_release);
}

Result<void> RuntimeDaemon::start() {
  if (started_.load(std::memory_order_acquire)) {
    return Error{Errc::Busy, "RuntimeDaemon already started"};
  }
  if (config_.node_id < 1 || config_.node_id > 31) {
    exit_code_.store(DaemonExitCode::ConfigError, std::memory_order_release);
    return Error{Errc::InvalidArgument, "node-id must be 1..31"};
  }
  if (config_.period.count() <= 0 || config_.command_timeout.count() <= 0 ||
      config_.output_ack_timeout.count() <= 0 ||
      config_.heartbeat_timeout.count() <= 0) {
    exit_code_.store(DaemonExitCode::ConfigError, std::memory_order_release);
    return Error{Errc::InvalidArgument, "period/timeouts must be positive"};
  }
  if (config_.cpu_affinity < -1 || config_.cpu_affinity >= CPU_SETSIZE ||
      config_.event_queue_capacity == 0 || config_.max_events_per_tick == 0 ||
      config_.max_frames_per_wake == 0 || config_.trace_capacity == 0) {
    exit_code_.store(DaemonExitCode::ConfigError, std::memory_order_release);
    return Error{Errc::InvalidArgument,
                 "CPU affinity must be in range and queue/trace capacities "
                 "must be positive"};
  }

  const auto probe = probe_can_interface(config_.can_if);
  if (probe != CanInterfaceStatus::Available) {
    exit_code_.store(DaemonExitCode::InterfaceError, std::memory_order_release);
    return Error{Errc::NotOpen,
                 "CAN interface missing or not ARPHRD_CAN: " + config_.can_if};
  }

  auto signals = SignalFd::block_and_open_shutdown_signals();
  if (!signals) {
    exit_code_.store(DaemonExitCode::InterfaceError, std::memory_order_release);
    return signals.error();
  }
  signals_ = std::move(signals.value());

  auto stop_event = EventFd::create();
  if (!stop_event) {
    signals_ = SignalFd{};
    exit_code_.store(DaemonExitCode::InterfaceError, std::memory_order_release);
    return stop_event.error();
  }
  stop_event_ = std::move(stop_event.value());

  queue_ = std::make_unique<BoundedInputQueue>(config_.event_queue_capacity);

  RuntimeConfig runtime_config{};
  runtime_config.scheduler.period = config_.period;
  runtime_config.scheduler.fifo_priority = config_.fifo_priority;
  runtime_config.scheduler.require_fifo = config_.require_fifo;
  runtime_config.scheduler.cpu_affinity = config_.cpu_affinity;
  runtime_config.command_timeout = config_.command_timeout;
  runtime_config.output_ack_timeout = config_.output_ack_timeout;
  runtime_config.trace_capacity = config_.trace_capacity;
  runtime_config.test_throw_on_tick = config_.test_throw_on_tick;
  runtime_ = std::make_unique<LinuxRuntime>(runtime_config);

  NodeSupervisorConfig supervisor_config{};
  supervisor_config.node_id = config_.node_id;
  supervisor_config.heartbeat_timeout = config_.heartbeat_timeout;
  supervisor_config.max_events_per_tick = config_.max_events_per_tick;
  supervisor_ = std::make_unique<NodeSupervisor>(supervisor_config, *queue_);

  runtime_->set_supervision_hook(
      [this](std::int64_t now_ns) { supervisor_->on_tick(*runtime_, now_ns); });

  auto started_runtime = runtime_->start();
  if (!started_runtime) {
    DaemonExitCode mapped = DaemonExitCode::WorkerFailure;
    if (started_runtime.error().code() == Errc::InvalidArgument) {
      mapped = DaemonExitCode::ConfigError;
    } else if (started_runtime.error().code() == Errc::Rejected) {
      mapped = DaemonExitCode::PermissionError;
    }
    exit_code_.store(mapped, std::memory_order_release);
    log_line("error", started_runtime.error().message());
    rollback_started_parts();
    return started_runtime.error();
  }

  // 独立 rcrd 没有上层 Adapter 代为 Boot；必须在 CAN 事件进入前先到 Idle。
  // 这不是 Activate：联锁、节点在线和显式激活仍然是打开普通输出的必要条件。
  const auto booted = runtime_->handle(RuntimeEvent::Boot);
  if (!booted.accepted) {
    exit_code_.store(DaemonExitCode::WorkerFailure, std::memory_order_release);
    rollback_started_parts();
    return Error{Errc::Rejected, "runtime Boot failed during daemon startup"};
  }

  const auto snap = runtime_->snapshot();
  log_line(
      "info",
      std::string("scheduler started fifo_enabled=") +
          (snap.scheduler.fifo_enabled ? "1" : "0") +
          " fifo_error=" + std::to_string(snap.scheduler.fifo_error) +
          " affinity_enabled=" + (snap.scheduler.affinity_enabled ? "1" : "0") +
          " affinity_error=" + std::to_string(snap.scheduler.affinity_error));

  CanIoConfig io_config{};
  io_config.can_if = config_.can_if;
  io_config.node_id = config_.node_id;
  io_config.max_frames_per_wake = config_.max_frames_per_wake;
  io_config.cpu_affinity = config_.cpu_affinity;
  io_ = std::make_unique<CanIoLoop>(io_config, *runtime_, *queue_, stop_event_,
                                    signals_);

  auto io_started = io_->start();
  if (!io_started) {
    DaemonExitCode mapped = DaemonExitCode::InterfaceError;
    if (io_started.error().code() == Errc::InvalidArgument) {
      mapped = DaemonExitCode::ConfigError;
    } else if (io_started.error().code() == Errc::Rejected) {
      mapped = DaemonExitCode::PermissionError;
    }
    exit_code_.store(mapped, std::memory_order_release);
    log_line("error", io_started.error().message());
    rollback_started_parts();
    return io_started.error();
  }

  stop_requested_.store(false, std::memory_order_release);
  exit_code_.store(DaemonExitCode::Ok, std::memory_order_release);
  started_.store(true, std::memory_order_release);

  if (config_.duration.count() > 0) {
    duration_thread_ = std::thread([this] { watch_duration(); });
  }

  log_line("info", "daemon started can=" + config_.can_if +
                       " node_id=" + std::to_string(config_.node_id));
  return Result<void>::success();
}

void RuntimeDaemon::watch_duration() {
  const auto deadline = std::chrono::steady_clock::now() + config_.duration;
  while (!stop_requested_.load(std::memory_order_acquire)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      log_line("info", "duration elapsed; requesting stop");
      request_stop();
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
}

void RuntimeDaemon::request_stop() {
  stop_requested_.store(true, std::memory_order_release);
  if (io_) {
    io_->request_stop();
  }
  wait_cv_.notify_all();
}

DaemonExitCode RuntimeDaemon::classify_stop() const {
  if (!io_) {
    return exit_code();
  }
  const auto reason = io_->stop_reason();
  if (reason == IoStopReason::IoError || reason == IoStopReason::SendFailure) {
    return DaemonExitCode::WorkerFailure;
  }
  if (runtime_) {
    const auto snap = runtime_->snapshot();
    if (snap.scheduler.worker_error != 0) {
      return DaemonExitCode::WorkerFailure;
    }
  }
  return DaemonExitCode::Ok;
}

DaemonExitCode RuntimeDaemon::wait_and_stop() {
  if (!started_.load(std::memory_order_acquire)) {
    return exit_code();
  }

  // scheduler worker 异常不会自动关闭 epoll，因此等待循环必须同时观察两条
  // worker。 否则 I/O 仍健康时主进程永远到不了 classify_stop()。
  while (io_ && io_->running() &&
         !stop_requested_.load(std::memory_order_acquire)) {
    if (runtime_) {
      const auto runtime_snap = runtime_->snapshot();
      if (!runtime_snap.running && runtime_snap.scheduler.worker_error != 0) {
        break;
      }
    }
    std::unique_lock lock(wait_mutex_);
    wait_cv_.wait_for(lock, std::chrono::milliseconds{20}, [this] {
      return stop_requested_.load(std::memory_order_acquire) || !io_ ||
             !io_->running();
    });
  }

  const DaemonExitCode code = classify_stop();
  exit_code_.store(code, std::memory_order_release);
  stop();
  return code;
}

void RuntimeDaemon::stop() {
  stop_requested_.store(true, std::memory_order_release);
  if (io_) {
    io_->request_stop();
    io_->join();
  }
  if (duration_thread_.joinable()) {
    duration_thread_.join();
  }
  if (runtime_) {
    const auto before = runtime_->snapshot();
    if (before.mode != RuntimeMode::Disabled) {
      log_line("info", std::string("stopping runtime mode=") +
                           std::string(to_string(before.mode)));
    }
    runtime_->stop();
  }
  if (io_) {
    const auto io_stats = io_->stats();
    log_line("info",
             std::string("io stopped reason=") +
                 std::string(to_string(io_stats.stop_reason)) +
                 " rx=" + std::to_string(io_stats.frames_received) +
                 " tx=" + std::to_string(io_stats.frames_sent) +
                 " decode_reject=" + std::to_string(io_stats.decode_rejects));
  }
  io_.reset();
  supervisor_.reset();
  runtime_.reset();
  queue_.reset();
  stop_event_ = EventFd{};
  signals_ = SignalFd{};
  started_.store(false, std::memory_order_release);
}

TransitionResult RuntimeDaemon::boot() {
  if (!runtime_) {
    return TransitionResult{false, RuntimeMode::Disabled, RuntimeMode::Disabled,
                            "daemon not started"};
  }
  const auto snap = runtime_->snapshot();
  if (snap.mode == RuntimeMode::Idle) {
    return TransitionResult{true, RuntimeMode::Idle, RuntimeMode::Idle,
                            "daemon already booted"};
  }
  return runtime_->handle(RuntimeEvent::Boot);
}

TransitionResult RuntimeDaemon::activate() {
  if (!runtime_) {
    return TransitionResult{false, RuntimeMode::Disabled, RuntimeMode::Disabled,
                            "daemon not started"};
  }
  return runtime_->handle(RuntimeEvent::ActivateRequest);
}

TransitionResult RuntimeDaemon::deactivate() {
  if (!runtime_) {
    return TransitionResult{false, RuntimeMode::Disabled, RuntimeMode::Disabled,
                            "daemon not started"};
  }
  return runtime_->handle(RuntimeEvent::DeactivateRequest);
}

TransitionResult RuntimeDaemon::clear_fault() {
  if (!runtime_) {
    return TransitionResult{false, RuntimeMode::Disabled, RuntimeMode::Disabled,
                            "daemon not started"};
  }
  const auto runtime_snap = runtime_->snapshot();
  if (runtime_snap.mode != RuntimeMode::Fault) {
    return runtime_->handle(RuntimeEvent::FaultCleared);
  }
  if (io_ && (io_->stop_reason() == IoStopReason::IoError ||
              io_->stop_reason() == IoStopReason::SendFailure)) {
    return TransitionResult{false, runtime_snap.mode, runtime_snap.mode,
                            "I/O failure requires daemon restart"};
  }
  if (supervisor_) {
    const auto recovery =
        supervisor_->acknowledge_fault_clear(runtime_snap.fault);
    if (!recovery) {
      return TransitionResult{false, runtime_snap.mode, runtime_snap.mode,
                              recovery.error().message()};
    }
  }
  return runtime_->handle(RuntimeEvent::FaultCleared);
}

Result<void>
RuntimeDaemon::publish_output_command(const OutputCommand &command) {
  if (!runtime_) {
    return Error{Errc::NotOpen, "daemon not started"};
  }
  return runtime_->publish_output_command(command);
}

DaemonSnapshot RuntimeDaemon::snapshot() const {
  DaemonSnapshot out{};
  out.exit_code = exit_code();
  out.started = started();
  out.stopping = stop_requested_.load(std::memory_order_acquire);
  if (runtime_) {
    out.runtime = runtime_->snapshot();
  }
  if (supervisor_) {
    out.node = supervisor_->snapshot();
  }
  if (io_) {
    out.io = io_->stats();
  }
  return out;
}

} // namespace rcr
