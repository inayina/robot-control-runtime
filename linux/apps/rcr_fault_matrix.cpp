// 自动故障矩阵：把 SPEC §12 与 P2 扩展场景变成可重复 PASS/FAIL。
// 缺 vcan0 时 vcan 场景硬失败（不是 Skip）。Fault Injection 仅经模拟器/测试参数。
#include "rcr/can_bus.hpp"
#include "rcr/can_v1.hpp"
#include "rcr/mailbox.hpp"
#include "rcr/node_sim.hpp"
#include "rcr/runtime.hpp"
#include "rcr/runtime_daemon.hpp"
#include "rcr/runtime_events.hpp"
#include "rcr/scheduler.hpp"
#include "rcr/time.hpp"
#include "rcr/vcan.hpp"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct Options {
  std::string can_if{"vcan0"};
  std::string sim_path{};
  std::string rcrd_path{};
  std::string evidence_path{};
};

struct ScenarioResult {
  std::string name;
  std::string result;  // pass|failed|permission_denied|unsupported|not_run
  std::string detail;
};

class ChildProcess {
 public:
  ~ChildProcess() { stop(); }

  bool start(const std::string& path, const std::vector<std::string>& args) {
    stop();
    const pid_t pid = ::fork();
    if (pid < 0) {
      return false;
    }
    if (pid == 0) {
      std::vector<char*> argv;
      argv.push_back(const_cast<char*>(path.c_str()));
      for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
      }
      argv.push_back(nullptr);
      ::execv(path.c_str(), argv.data());
      std::_Exit(127);
    }
    pid_ = pid;
    return true;
  }

  void stop() {
    if (pid_ <= 0) {
      return;
    }
    ::kill(pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
      if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    ::kill(pid_, SIGKILL);
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
  }

  [[nodiscard]] pid_t pid() const noexcept { return pid_; }

  /// 放弃回收责任（调用方已 waitpid）；避免析构再次 kill。
  void detach() noexcept { pid_ = -1; }

 private:
  pid_t pid_{-1};
};

bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds budget) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return pred();
}

std::string git_commit() {
  FILE* pipe = ::popen("git rev-parse HEAD 2>/dev/null", "r");
  if (!pipe) {
    return "unknown";
  }
  char buf[128]{};
  std::string out;
  if (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
    out = buf;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
      out.pop_back();
    }
  }
  ::pclose(pipe);
  return out.empty() ? "unknown" : out;
}

bool git_dirty() {
  FILE* pipe = ::popen("git status --porcelain 2>/dev/null", "r");
  if (!pipe) {
    return true;
  }
  char buf[8]{};
  const bool dirty = std::fgets(buf, sizeof(buf), pipe) != nullptr;
  ::pclose(pipe);
  return dirty;
}

ScenarioResult pass(std::string name, std::string detail) {
  return {std::move(name), "pass", std::move(detail)};
}
ScenarioResult fail(std::string name, std::string detail) {
  return {std::move(name), "failed", std::move(detail)};
}

ScenarioResult scenario_activate_before_boot() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("activate_before_boot", "start failed");
  }
  const auto tr = rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const auto mode = rt.snapshot().mode;
  rt.stop();
  if (!tr.accepted && mode == rcr::RuntimeMode::Disabled) {
    return pass("activate_before_boot", tr.reason);
  }
  return fail("activate_before_boot", "expected reject in Disabled");
}

ScenarioResult scenario_activate_without_scheduler() {
  rcr::LinuxRuntime rt;
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  const auto tr = rt.handle(rcr::RuntimeEvent::ActivateRequest);
  if (!tr.accepted) {
    return pass("activate_without_scheduler", tr.reason);
  }
  return fail("activate_without_scheduler", "expected reject when scheduler stopped");
}

ScenarioResult scenario_activate_without_interlock() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start()) {
    return fail("activate_without_interlock", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  const auto tr = rt.handle(rcr::RuntimeEvent::ActivateRequest);
  rt.stop();
  if (!tr.accepted && rt.snapshot().mode != rcr::RuntimeMode::Active) {
    return pass("activate_without_interlock", tr.reason);
  }
  return fail("activate_without_interlock", "expected reject");
}

ScenarioResult scenario_expired_deadline() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("expired_deadline", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const auto now = rcr::monotonic_now_ns();
  if (!now) {
    rt.stop();
    return fail("expired_deadline", "clock failed");
  }
  rcr::OutputCommand cmd{};
  cmd.session_id = 1;
  cmd.sequence = 1;
  cmd.mask = 1;
  cmd.values = 1;
  cmd.deadline_ns = now.value() - 1;
  const auto pub = rt.publish_output_command(cmd);
  rt.stop();
  if (!pub.ok() && pub.error().code() == rcr::Errc::Rejected) {
    return pass("expired_deadline", pub.error().message());
  }
  return fail("expired_deadline", "expected reject");
}

ScenarioResult scenario_stale_sequence() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("stale_sequence", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const auto now = rcr::monotonic_now_ns().value();
  rcr::OutputCommand cmd{};
  cmd.session_id = 1;
  cmd.sequence = 2;
  cmd.mask = 1;
  cmd.values = 1;
  cmd.deadline_ns = now + 500'000'000LL;
  if (!rt.publish_output_command(cmd).ok()) {
    rt.stop();
    return fail("stale_sequence", "first publish failed");
  }
  cmd.sequence = 1;
  const auto second = rt.publish_output_command(cmd);
  rt.stop();
  if (!second.ok()) {
    return pass("stale_sequence", second.error().message());
  }
  return fail("stale_sequence", "expected reject");
}

ScenarioResult scenario_session_mismatch() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("session_mismatch", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const auto now = rcr::monotonic_now_ns().value();
  rcr::OutputCommand cmd{};
  cmd.session_id = 1;
  cmd.sequence = 1;
  cmd.mask = 1;
  cmd.values = 1;
  cmd.deadline_ns = now + 500'000'000LL;
  if (!rt.publish_output_command(cmd).ok()) {
    rt.stop();
    return fail("session_mismatch", "first publish failed");
  }
  cmd.session_id = 2;
  cmd.sequence = 2;
  const auto second = rt.publish_output_command(cmd);
  rt.stop();
  if (!second.ok()) {
    return pass("session_mismatch", second.error().message());
  }
  return fail("session_mismatch", "expected reject");
}

ScenarioResult scenario_command_timeout_hold() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{5};
  cfg.command_timeout = std::chrono::milliseconds{30};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("command_timeout_hold", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const auto now = rcr::monotonic_now_ns();
  if (!now.ok()) {
    rt.stop();
    return fail("command_timeout_hold", "clock failed");
  }
  rcr::OutputCommand cmd{};
  cmd.session_id = 1;
  cmd.sequence = 1;
  cmd.mask = 0x01;
  cmd.values = 0x01;
  cmd.deadline_ns = now.value() + 500'000'000LL;
  if (!rt.publish_output_command(cmd).ok()) {
    rt.stop();
    return fail("command_timeout_hold", "publish failed");
  }
  const bool held = wait_until(
      [&] {
        const auto snap = rt.snapshot();
        return snap.mode == rcr::RuntimeMode::Hold && snap.fault == rcr::FaultCode::Watchdog;
      },
      std::chrono::milliseconds{500});
  rt.stop();
  if (held) {
    return pass("command_timeout_hold", "Hold+Watchdog");
  }
  return fail("command_timeout_hold", "timeout did not enter Hold");
}

ScenarioResult scenario_output_ack_timeout_hold() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{1};
  cfg.command_timeout = std::chrono::seconds{1};
  cfg.output_ack_timeout = std::chrono::milliseconds{8};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("output_ack_timeout_hold", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);

  const auto now = rcr::monotonic_now_ns();
  if (!now) {
    rt.stop();
    return fail("output_ack_timeout_hold", "clock read failed");
  }
  rcr::OutputCommand command{};
  command.session_id = 1;
  command.sequence = 1;
  command.deadline_ns = now.value() + 500'000'000LL;
  command.mask = 1;
  command.values = 1;
  if (!rt.publish_output_command(command).ok() ||
      !rt.try_consume_output_command().has_value() ||
      !rt.note_output_command_sent(1, 1, now.value()).ok()) {
    rt.stop();
    return fail("output_ack_timeout_hold", "failed to establish pending ACK");
  }

  const bool held = wait_until(
      [&] {
        const auto snap = rt.snapshot();
        return snap.mode == rcr::RuntimeMode::Hold &&
               snap.fault == rcr::FaultCode::AckTimeout &&
               snap.ack_timeout_count == 1 && !snap.output_ack_pending;
      },
      std::chrono::milliseconds{500});
  const auto resume = rt.handle(rcr::RuntimeEvent::Resume);
  const auto final_mode = rt.snapshot().mode;
  rt.stop();
  if (held && resume.accepted && final_mode == rcr::RuntimeMode::Idle) {
    return pass("output_ack_timeout_hold",
                "Hold+AckTimeout; explicit Resume returns only Idle");
  }
  return fail("output_ack_timeout_hold",
              "ACK timeout classification or recovery contract failed");
}

ScenarioResult scenario_interlock_lost_hold() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("interlock_lost_hold", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  rt.set_interlock_ready(false);
  const auto snap = rt.snapshot();
  rt.stop();
  if (snap.mode == rcr::RuntimeMode::Hold && snap.fault == rcr::FaultCode::InterlockLost) {
    return pass("interlock_lost_hold", "Hold+InterlockLost");
  }
  return fail("interlock_lost_hold", "expected Hold");
}

ScenarioResult scenario_estop_latch() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{10};
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("estop_latch", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  (void)rt.handle(rcr::RuntimeEvent::EStopTrigger);
  const auto activate = rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const auto mode = rt.snapshot().mode;
  rt.stop();
  if (mode == rcr::RuntimeMode::EStop && !activate.accepted) {
    return pass("estop_latch", "EStop blocks Activate");
  }
  return fail("estop_latch", "EStop not latched");
}

ScenarioResult scenario_queue_overflow_fault() {
  rcr::BoundedInputQueue queue{1};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime rt;
  if (!rt.start().ok()) {
    return fail("queue_overflow_fault", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  rcr::RuntimeInputEvent e{};
  e.kind = rcr::RuntimeInputKind::Heartbeat;
  e.node_id = 1;
  e.boot_id = 1;
  e.session_id = 1;
  e.hb_seq = 1;
  e.monotonic_ns = 1;
  (void)queue.try_push(e);
  (void)queue.try_push(e);
  supervisor.on_tick(rt, 1);
  const auto snap = rt.snapshot();
  const auto nsnap = supervisor.snapshot();
  rt.stop();
  if (nsnap.overflow_fault_latched && snap.mode == rcr::RuntimeMode::Fault &&
      snap.fault == rcr::FaultCode::Internal) {
    return pass("queue_overflow_fault", "Internal fault latched");
  }
  return fail("queue_overflow_fault", "overflow did not fault");
}

ScenarioResult scenario_queue_overflow_clear_rejected() {
  rcr::BoundedInputQueue queue{1};
  rcr::NodeSupervisorConfig cfg{};
  cfg.node_id = 1;
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime rt;
  if (!rt.start().ok()) {
    return fail("queue_overflow_clear_rejected", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rcr::RuntimeInputEvent event{};
  event.kind = rcr::RuntimeInputKind::Heartbeat;
  event.node_id = 1;
  event.boot_id = 1;
  event.session_id = 1;
  event.hb_seq = 1;
  event.monotonic_ns = 1;
  (void)queue.try_push(event);
  (void)queue.try_push(event);
  supervisor.on_tick(rt, 1);
  const auto recovery = supervisor.acknowledge_fault_clear(rcr::FaultCode::Internal);
  const auto clear = recovery ? rt.handle(rcr::RuntimeEvent::FaultCleared)
                              : rcr::TransitionResult{};
  const auto snap = rt.snapshot();
  rt.stop();
  if (!recovery.ok() && !clear.accepted && snap.mode == rcr::RuntimeMode::Fault) {
    return pass("queue_overflow_clear_rejected", "overflow latch requires daemon restart");
  }
  return fail("queue_overflow_clear_rejected", "persistent overflow was cleared");
}

ScenarioResult scenario_overlapping_fault_recovery_rejected() {
  rcr::BoundedInputQueue queue{1};
  rcr::NodeSupervisorConfig cfg{};
  cfg.heartbeat_timeout = std::chrono::milliseconds{20};
  rcr::NodeSupervisor supervisor{cfg, queue};
  rcr::LinuxRuntime rt;
  if (!rt.start().ok()) {
    return fail("overlapping_fault_recovery_rejected", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);

  rcr::RuntimeInputEvent hb{};
  hb.kind = rcr::RuntimeInputKind::Heartbeat;
  hb.node_id = 1;
  hb.boot_id = 1;
  hb.session_id = 1;
  hb.hb_seq = 1;
  hb.monotonic_ns = 100;
  (void)queue.try_push(hb);
  (void)queue.try_push(hb);  // overflow → Internal
  supervisor.on_tick(rt, 100);
  supervisor.on_tick(rt, 30'000'100);  // later CommLoss overwrites classification

  hb.hb_seq = 2;
  hb.monotonic_ns = 30'000'200;
  (void)queue.try_push(hb);
  supervisor.on_tick(rt, hb.monotonic_ns);
  const auto recovery =
      supervisor.acknowledge_fault_clear(rcr::FaultCode::CommLoss);
  const auto snap = rt.snapshot();
  rt.stop();
  if (!recovery.ok() && snap.mode == rcr::RuntimeMode::Fault &&
      supervisor.snapshot().overflow_fault_latched) {
    return pass("overlapping_fault_recovery_rejected",
                "recovered CommLoss cannot bypass overflow restart gate");
  }
  return fail("overlapping_fault_recovery_rejected",
              "last FaultCode bypassed a persistent blocker");
}

ScenarioResult scenario_node_output_lease_neutral() {
  rcr::CanNodeLogic node({});
  rcr::can_v1::WireOutputCommand command{};
  command.node_id = 1;
  command.mask = 0xFF;
  command.session_id = 1;
  command.sequence = 1;
  command.values = 0xA5;
  command.validity_10ms = 1;
  const auto applied = node.apply_command(command, 0, 0);
  const bool held_before_deadline =
      !node.expire_output_lease(9'999'999) && node.output_bits() == 0xA5;
  const bool neutral_at_deadline =
      node.expire_output_lease(10'000'000) && node.output_bits() == 0;
  if (applied.status.result == rcr::can_v1::OutputResult::Applied &&
      held_before_deadline && neutral_at_deadline) {
    return pass("node_output_lease_neutral",
                "ordinary output neutral at declared deadline");
  }
  return fail("node_output_lease_neutral",
              "applied output survived its authority lease");
}

ScenarioResult scenario_worker_exception_fail_closed() {
  rcr::RuntimeConfig cfg{};
  cfg.scheduler.period = std::chrono::milliseconds{5};
  cfg.test_throw_on_tick = true;
  rcr::LinuxRuntime rt{cfg};
  if (!rt.start().ok()) {
    return fail("worker_exception_fail_closed", "start failed");
  }
  (void)rt.handle(rcr::RuntimeEvent::Boot);
  rt.set_interlock_ready(true);
  (void)rt.handle(rcr::RuntimeEvent::ActivateRequest);
  const bool stopped = wait_until([&] { return !rt.snapshot().running; },
                                  std::chrono::milliseconds{500});
  const auto now = rcr::monotonic_now_ns().value();
  rcr::OutputCommand cmd{};
  cmd.session_id = 1;
  cmd.sequence = 1;
  cmd.mask = 1;
  cmd.values = 1;
  cmd.deadline_ns = now + 500'000'000LL;
  const auto pub = rt.publish_output_command(cmd);
  const int worker_error = rt.snapshot().scheduler.worker_error;
  rt.stop();
  if (stopped && !pub.ok() && worker_error != 0) {
    return pass("worker_exception_fail_closed", "publish rejected after worker stop");
  }
  return fail("worker_exception_fail_closed", "worker fail-closed incomplete");
}

ScenarioResult scenario_mailbox_overwrite() {
  rcr::CommandMailbox box;
  rcr::OutputCommand a{};
  a.session_id = 1;
  a.sequence = 1;
  a.mask = 1;
  a.values = 1;
  a.deadline_ns = 100;
  rcr::OutputCommand b = a;
  b.sequence = 2;
  b.values = 2;
  box.publish(a);
  box.publish(b);
  auto got = box.try_consume();
  if (got && got->sequence == 2 && box.drop_count() == 1) {
    return pass("mailbox_overwrite", "latest-wins drop_count=1");
  }
  return fail("mailbox_overwrite", "overwrite semantics broken");
}

ScenarioResult scenario_fifo_permission_observed() {
  rcr::SchedulerConfig cfg{};
  cfg.period = std::chrono::milliseconds{5};
  cfg.fifo_priority = 10;
  cfg.require_fifo = false;
  rcr::PeriodicScheduler sched{cfg};
  auto started = sched.start([](const rcr::SchedulerTick&) {});
  if (!started) {
    return fail("fifo_permission_observed", started.error().message());
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{30});
  sched.request_stop();
  sched.join();
  const auto st = sched.stats();
  if (st.fifo_enabled) {
    return pass("fifo_permission_observed", "FIFO enabled");
  }
  if (st.fifo_error == EPERM || st.fifo_error == EACCES) {
    ScenarioResult r{"fifo_permission_observed", "permission_denied",
                     "fifo_error=" + std::to_string(st.fifo_error)};
    return r;
  }
  return fail("fifo_permission_observed",
              "unexpected fifo_error=" + std::to_string(st.fifo_error));
}

ScenarioResult scenario_daemon_worker_failure(const Options& options) {
  rcr::DaemonConfig cfg{};
  cfg.can_if = options.can_if;
  cfg.period = std::chrono::milliseconds{5};
  cfg.test_throw_on_tick = true;
  rcr::RuntimeDaemon daemon{cfg};
  const auto started = daemon.start();
  if (!started) {
    return fail("daemon_worker_failure", started.error().message());
  }
  const auto exit = daemon.wait_and_stop();
  if (exit == rcr::DaemonExitCode::WorkerFailure) {
    return pass("daemon_worker_failure", "scheduler failure escalated to daemon exit 4");
  }
  return fail("daemon_worker_failure", "daemon did not escalate scheduler failure");
}

ScenarioResult scenario_illegal_frame_reject() {
  rcr::CanFrame bad{};
  bad.can_id = rcr::can_v1::make_can_id(rcr::can_v1::Function::Heartbeat, 1);
  bad.len = 7;  // DLC 必须为 8
  const auto decoded = rcr::can_v1::decode(bad);
  if (!decoded.ok() && decoded.error().code() == rcr::Errc::Rejected) {
    return pass("illegal_frame_reject", decoded.error().message());
  }
  return fail("illegal_frame_reject", "expected protocol reject");
}

ScenarioResult scenario_comm_loss_vcan(const Options& options) {
  if (rcr::probe_can_interface(options.can_if) != rcr::CanInterfaceStatus::Available) {
    return fail("comm_loss_vcan", "CAN interface missing");
  }
  rcr::SocketCan probe{options.can_if};
  auto opened = probe.open();
  if (!opened) {
    return fail("comm_loss_vcan", "cannot open CAN socket: " + opened.error().message());
  }
  probe.close();

  ChildProcess sim;
  if (!sim.start(options.sim_path,
                 {"--can", options.can_if, "--node-id", "1", "--heartbeat-ms", "40",
                  "--duration-ms", "200"})) {
    return fail("comm_loss_vcan", "failed to start node sim");
  }

  rcr::DaemonConfig cfg{};
  cfg.can_if = options.can_if;
  cfg.node_id = 1;
  cfg.heartbeat_timeout = std::chrono::milliseconds{150};
  cfg.period = std::chrono::milliseconds{10};
  rcr::RuntimeDaemon daemon{cfg};
  if (!daemon.start()) {
    return fail("comm_loss_vcan", daemon.exit_code() == rcr::DaemonExitCode::InterfaceError
                                      ? "interface/start failed"
                                      : "start failed");
  }
  (void)daemon.boot();
  if (!wait_until([&] { return daemon.snapshot().node.online; },
                  std::chrono::milliseconds{1000})) {
    daemon.request_stop();
    (void)daemon.wait_and_stop();
    return fail("comm_loss_vcan", "node never online");
  }
  const bool lost = wait_until(
      [&] {
        return daemon.snapshot().node.comm_loss_latched ||
               daemon.snapshot().runtime.fault == rcr::FaultCode::CommLoss;
      },
      std::chrono::milliseconds{2000});
  daemon.request_stop();
  (void)daemon.wait_and_stop();
  if (lost) {
    return pass("comm_loss_vcan", "CommLoss after sim duration exit");
  }
  return fail("comm_loss_vcan", "CommLoss not observed");
}

ScenarioResult scenario_node_restart_vcan(const Options& options) {
  if (rcr::probe_can_interface(options.can_if) != rcr::CanInterfaceStatus::Available) {
    return fail("node_restart_vcan", "CAN interface missing");
  }
  ChildProcess sim;
  if (!sim.start(options.sim_path,
                 {"--can", options.can_if, "--node-id", "1", "--heartbeat-ms", "40",
                  "--fault-restart-after-ms", "250"})) {
    return fail("node_restart_vcan", "failed to start node sim");
  }
  rcr::DaemonConfig cfg{};
  cfg.can_if = options.can_if;
  cfg.node_id = 1;
  cfg.period = std::chrono::milliseconds{10};
  cfg.heartbeat_timeout = std::chrono::milliseconds{300};
  rcr::RuntimeDaemon daemon{cfg};
  if (!daemon.start()) {
    return fail("node_restart_vcan", "daemon start failed");
  }
  (void)daemon.boot();
  if (!wait_until([&] { return daemon.snapshot().node.online; },
                  std::chrono::milliseconds{1000})) {
    daemon.request_stop();
    (void)daemon.wait_and_stop();
    return fail("node_restart_vcan", "never online");
  }
  const bool restarted = wait_until(
      [&] { return daemon.snapshot().node.restart_latched; },
      std::chrono::milliseconds{2000});
  daemon.request_stop();
  (void)daemon.wait_and_stop();
  sim.stop();
  if (restarted) {
    return pass("node_restart_vcan", "restart_latched");
  }
  return fail("node_restart_vcan", "restart not observed");
}

ScenarioResult scenario_sigterm_rcrd(const Options& options) {
  if (rcr::probe_can_interface(options.can_if) != rcr::CanInterfaceStatus::Available) {
    return fail("sigterm_rcrd", "CAN interface missing");
  }
  ChildProcess daemon;
  if (!daemon.start(options.rcrd_path,
                    {"--can", options.can_if, "--node-id", "1", "--period-ms", "10"})) {
    return fail("sigterm_rcrd", "failed to spawn rcrd");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{300});
  if (daemon.pid() <= 0) {
    return fail("sigterm_rcrd", "rcrd pid missing");
  }
  ::kill(daemon.pid(), SIGTERM);
  int status = 0;
  bool exited = false;
  for (int i = 0; i < 150; ++i) {
    const pid_t rc = ::waitpid(daemon.pid(), &status, WNOHANG);
    if (rc == daemon.pid()) {
      exited = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  if (!exited) {
    daemon.stop();
    return fail("sigterm_rcrd", "did not exit in bound");
  }
  // waitpid 已回收子进程，显式放弃 ChildProcess 的停止责任，避免析构再向复用 pid 发信号。
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    daemon.detach();
    return pass("sigterm_rcrd", "exit 0");
  }
  daemon.detach();
  return fail("sigterm_rcrd", "exit status not 0");
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      return false;
    }
    if (i + 1 >= argc) {
      return false;
    }
    const std::string_view value(argv[++i]);
    if (arg == "--can") {
      options.can_if = std::string(value);
    } else if (arg == "--sim-path") {
      options.sim_path = std::string(value);
    } else if (arg == "--rcrd-path") {
      options.rcrd_path = std::string(value);
    } else if (arg == "--evidence") {
      options.evidence_path = std::string(value);
    } else {
      return false;
    }
  }
  return true;
}

void usage(const char* program) {
  std::cerr << "usage: " << program
            << " --sim-path PATH --rcrd-path PATH --evidence PATH [--can IFACE]\n";
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parse_options(argc, argv, options) || options.evidence_path.empty() ||
      options.sim_path.empty() || options.rcrd_path.empty()) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  {
    std::ifstream existing(options.evidence_path);
    if (existing.good()) {
      std::cerr << "error: evidence file already exists (refuse overwrite): "
                << options.evidence_path << "\n";
      return EXIT_FAILURE;
    }
  }

  if (rcr::probe_can_interface(options.can_if) != rcr::CanInterfaceStatus::Available) {
    std::cerr << "error: " << options.can_if << " missing (fault matrix requires vcan)\n";
    return EXIT_FAILURE;
  }

  std::vector<ScenarioResult> results;
  results.push_back(scenario_activate_before_boot());
  results.push_back(scenario_activate_without_scheduler());
  results.push_back(scenario_activate_without_interlock());
  results.push_back(scenario_expired_deadline());
  results.push_back(scenario_stale_sequence());
  results.push_back(scenario_session_mismatch());
  results.push_back(scenario_command_timeout_hold());
  results.push_back(scenario_output_ack_timeout_hold());
  results.push_back(scenario_interlock_lost_hold());
  results.push_back(scenario_estop_latch());
  results.push_back(scenario_queue_overflow_fault());
  results.push_back(scenario_queue_overflow_clear_rejected());
  results.push_back(scenario_overlapping_fault_recovery_rejected());
  results.push_back(scenario_node_output_lease_neutral());
  results.push_back(scenario_worker_exception_fail_closed());
  results.push_back(scenario_mailbox_overwrite());
  results.push_back(scenario_illegal_frame_reject());
  results.push_back(scenario_fifo_permission_observed());
  results.push_back(scenario_daemon_worker_failure(options));
  results.push_back(scenario_comm_loss_vcan(options));
  results.push_back(scenario_node_restart_vcan(options));
  results.push_back(scenario_sigterm_rcrd(options));

  int passed = 0;
  int failed = 0;
  int permission = 0;
  int unsupported = 0;
  int not_run = 0;
  for (const auto& r : results) {
    std::cout << "scenario=" << r.name << " result=" << r.result << " detail=" << r.detail
              << "\n";
    if (r.result == "pass") {
      ++passed;
    } else if (r.result == "failed") {
      ++failed;
    } else if (r.result == "permission_denied") {
      ++permission;
    } else if (r.result == "unsupported") {
      ++unsupported;
    } else if (r.result == "not_run") {
      ++not_run;
    } else {
      ++failed;
    }
  }

  utsname uts{};
  ::uname(&uts);
  std::ofstream ev(options.evidence_path);
  if (!ev) {
    std::cerr << "error: cannot write evidence\n";
    return EXIT_FAILURE;
  }
  ev << "date_utc=" << [] {
       char buf[64]{};
       const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
       std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
       return std::string(buf);
     }() << "\n"
     << "hostname=" << uts.nodename << "\n"
     << "os_kernel=" << uts.sysname << " " << uts.release << " " << uts.machine << "\n"
     << "git_commit=" << git_commit() << "\n"
     << "git_dirty=" << (git_dirty() ? "true" : "false") << "\n"
     << "can_if=" << options.can_if << "\n"
     << "scenarios_total=" << results.size() << "\n"
     << "scenarios_pass=" << passed << "\n"
     << "scenarios_failed=" << failed << "\n"
     << "scenarios_permission_denied=" << permission << "\n"
     << "scenarios_unsupported=" << unsupported << "\n"
     << "scenarios_not_run=" << not_run << "\n";
  for (const auto& r : results) {
    ev << "scenario." << r.name << ".result=" << r.result << "\n"
       << "scenario." << r.name << ".detail=" << r.detail << "\n";
  }

  std::cout << "==== fault matrix pass=" << passed << " failed=" << failed
            << " permission_denied=" << permission << " unsupported=" << unsupported
            << " not_run=" << not_run << " ====\n";
  // permission_denied 不算代码失败，但正式矩阵要求全部可解释；failed/not_run → 非零。
  return failed == 0 && not_run == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
