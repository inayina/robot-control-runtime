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

#include <pthread.h>
#include <sched.h>

namespace rcr {
namespace {

void log_line(std::string_view level, std::string_view message) {
  // 结构化程度保持最低：级别 + 消息。周期路径禁止调用本函数。
  std::cerr << "rcrd level=" << level << " msg=" << message << '\n';
}

}  // namespace

RuntimeDaemon::RuntimeDaemon(DaemonConfig config) : config_(std::move(config)) {}

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

void RuntimeDaemon::apply_scheduler_affinity() {
  // 周期线程已在 scheduler 内创建；affinity 在 start 成功后对当前进程不自动继承到
  // 既有线程。V1 在 I/O 线程内设置 affinity；周期线程 affinity 通过
  // pthread_setaffinity 需要 worker 回调，首版仅绑定 I/O（CanIoConfig.cpu_affinity）。
  // 若未来需要绑定 scheduler，应在 PeriodicScheduler worker 启动握手中设置。
  (void)config_.cpu_affinity;
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
      config_.heartbeat_timeout.count() <= 0) {
    exit_code_.store(DaemonExitCode::ConfigError, std::memory_order_release);
    return Error{Errc::InvalidArgument, "period/timeouts must be positive"};
  }

  const auto probe = probe_can_interface(config_.can_if);
  if (probe != CanInterfaceStatus::Available) {
    exit_code_.store(DaemonExitCode::InterfaceError, std::memory_order_release);
    return Error{Errc::NotOpen, "CAN interface missing or not ARPHRD_CAN: " + config_.can_if};
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
  runtime_config.command_timeout = config_.command_timeout;
  runtime_config.trace_capacity = config_.trace_capacity;
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
    } else if (config_.require_fifo) {
      mapped = DaemonExitCode::PermissionError;
    }
    exit_code_.store(mapped, std::memory_order_release);
    log_line("error", started_runtime.error().message());
    rollback_started_parts();
    return started_runtime.error();
  }

  const auto snap = runtime_->snapshot();
  log_line("info", std::string("scheduler started fifo_enabled=") +
                       (snap.scheduler.fifo_enabled ? "1" : "0") +
                       " fifo_error=" + std::to_string(snap.scheduler.fifo_error));

  CanIoConfig io_config{};
  io_config.can_if = config_.can_if;
  io_config.node_id = config_.node_id;
  io_config.max_frames_per_wake = config_.max_frames_per_wake;
  io_config.cpu_affinity = config_.cpu_affinity;
  io_ = std::make_unique<CanIoLoop>(io_config, *runtime_, *queue_, stop_event_, signals_);

  auto io_started = io_->start();
  if (!io_started) {
    exit_code_.store(DaemonExitCode::InterfaceError, std::memory_order_release);
    log_line("error", io_started.error().message());
    rollback_started_parts();
    return io_started.error();
  }

  stop_requested_.store(false, std::memory_order_release);
  exit_code_.store(DaemonExitCode::Ok, std::memory_order_release);
  started_.store(true, std::memory_order_release);
  apply_scheduler_affinity();

  if (config_.duration.count() > 0) {
    duration_thread_ = std::thread([this] { watch_duration(); });
  }

  log_line("info", "daemon started can=" + config_.can_if +
                       " node_id=" + std::to_string(config_.node_id));
  return Result<void>::success();
}

void RuntimeDaemon::watch_duration() {
  const auto deadline =
      std::chrono::steady_clock::now() + config_.duration;
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

  // I/O 线程退出即表示信号或内部 stop；主线程轮询 running 标志。
  while (io_ && io_->running() && !stop_requested_.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
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
    log_line("info", std::string("io stopped reason=") +
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
  return runtime_->handle(RuntimeEvent::FaultCleared);
}

Result<void> RuntimeDaemon::publish_output_command(const OutputCommand& command) {
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

}  // namespace rcr
