// Linux 目标测试：RuntimeDaemon 同进程服务 API（需 vcan0 时跑端到端场景）。
#include "rcr/can_bus.hpp"
#include "rcr/can_v1.hpp"
#include "rcr/runtime_daemon.hpp"
#include "rcr/time.hpp"
#include "rcr/vcan.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef RCR_NODE_SIM_PATH
#define RCR_NODE_SIM_PATH "./rcr_node_sim"
#endif

namespace {

std::string find_node_sim() {
  const char* env = std::getenv("RCR_NODE_SIM");
  if (env && *env) {
    return env;
  }
  return std::string(RCR_NODE_SIM_PATH);
}

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
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
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

 private:
  pid_t pid_{-1};
};

void require_vcan_or_skip() {
  if (rcr::probe_can_interface("vcan0") != rcr::CanInterfaceStatus::Available) {
    RCR_SKIP("vcan0 missing");
  }
  // sysfs 可见不等于当前进程能打开 PF_CAN（沙箱/权限不足时应 Skip，而非假 FAIL）。
  rcr::SocketCan probe_bus{"vcan0"};
  auto opened = probe_bus.open();
  if (!opened) {
    RCR_SKIP(std::string("cannot open vcan0: ") + opened.error().message());
  }
  probe_bus.close();
}

bool env_flag_enabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && std::string(value) != "0";
}

bool run_ip_link(const char* iface, const char* action) {
  // 测试/运维侧才允许改链路；库代码仍只做只读 probe。
  const pid_t pid = ::fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    ::execlp("ip", "ip", "link", "set", "dev", iface, action, static_cast<char*>(nullptr));
    std::_Exit(127);
  }
  int status = 0;
  if (::waitpid(pid, &status, 0) != pid) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// 确保异常路径也会把接口拉回 up，避免弄坏后续依赖 vcan0 的测试。
class VcanLinkDownGuard {
 public:
  explicit VcanLinkDownGuard(const char* iface) : iface_(iface) {}
  ~VcanLinkDownGuard() {
    if (lowered_) {
      (void)run_ip_link(iface_, "up");
    }
  }

  bool down() {
    if (!run_ip_link(iface_, "down")) {
      return false;
    }
    lowered_ = true;
    return true;
  }

 private:
  const char* iface_;
  bool lowered_{false};
};

void require_iface_down_authorization_or_skip() {
  // 故意改主机链路状态；默认 CTest 不得执行。显式授权：RCR_ALLOW_IFACE_DOWN=1。
  if (!env_flag_enabled("RCR_ALLOW_IFACE_DOWN")) {
    RCR_SKIP("set RCR_ALLOW_IFACE_DOWN=1 to authorize vcan link-down fault injection");
  }
}

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

}  // namespace

RCR_TEST(DaemonRejectsMissingInterface) {
  rcr::DaemonConfig cfg{};
  cfg.can_if = "definitely_missing_can_iface";
  cfg.duration = std::chrono::milliseconds{100};
  rcr::RuntimeDaemon daemon{cfg};
  auto started = daemon.start();
  RCR_EXPECT(!started.ok());
  RCR_EXPECT(daemon.exit_code() == rcr::DaemonExitCode::InterfaceError);
}

RCR_TEST(DaemonConfigErrorOnBadNodeId) {
  rcr::DaemonConfig cfg{};
  cfg.node_id = 0;
  rcr::RuntimeDaemon daemon{cfg};
  auto started = daemon.start();
  RCR_EXPECT(!started.ok());
  RCR_EXPECT(daemon.exit_code() == rcr::DaemonExitCode::ConfigError);
}

RCR_TEST(DaemonConfigErrorOnOutOfRangeAffinity) {
  rcr::DaemonConfig cfg{};
  cfg.cpu_affinity = CPU_SETSIZE;
  rcr::RuntimeDaemon daemon{cfg};
  const auto started = daemon.start();
  RCR_EXPECT(!started.ok());
  RCR_EXPECT(daemon.exit_code() == rcr::DaemonExitCode::ConfigError);
}

RCR_TEST(DaemonStartsBootedInIdle) {
  require_vcan_or_skip();
  rcr::DaemonConfig cfg{};
  cfg.can_if = "vcan0";
  cfg.duration = std::chrono::milliseconds{50};
  rcr::RuntimeDaemon daemon{cfg};
  RCR_REQUIRE(daemon.start().ok());
  RCR_EXPECT(daemon.snapshot().runtime.mode == rcr::RuntimeMode::Idle);
  RCR_EXPECT(daemon.wait_and_stop() == rcr::DaemonExitCode::Ok);
}

RCR_TEST(DaemonEscalatesSchedulerWorkerFailure) {
  require_vcan_or_skip();
  rcr::DaemonConfig cfg{};
  cfg.can_if = "vcan0";
  cfg.period = std::chrono::milliseconds{2};
  cfg.test_throw_on_tick = true;
  rcr::RuntimeDaemon daemon{cfg};
  RCR_REQUIRE(daemon.start().ok());
  RCR_EXPECT(daemon.wait_and_stop() == rcr::DaemonExitCode::WorkerFailure);
}

RCR_TEST(DaemonOnlineHeartbeatAndBoundedStop) {
  require_vcan_or_skip();

  ChildProcess sim;
  RCR_REQUIRE(sim.start(find_node_sim(),
                        {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms", "50"}));

  rcr::DaemonConfig cfg{};
  cfg.can_if = "vcan0";
  cfg.node_id = 1;
  cfg.period = std::chrono::milliseconds{10};
  cfg.heartbeat_timeout = std::chrono::milliseconds{300};
  rcr::RuntimeDaemon daemon{cfg};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(daemon.boot().accepted);

  const bool online = wait_until(
      [&] {
        const auto snap = daemon.snapshot();
        return snap.node.online && snap.node.heartbeats > 0;
      },
      std::chrono::milliseconds{2000});
  RCR_EXPECT(online);

  const bool interlock = wait_until(
      [&] { return daemon.snapshot().runtime.interlock_ready; },
      std::chrono::milliseconds{2000});
  RCR_EXPECT(interlock);
  RCR_REQUIRE(daemon.activate().accepted);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  rcr::OutputCommand cmd{};
  cmd.session_id = daemon.snapshot().node.session_id;
  cmd.sequence = 1;
  cmd.mask = 0x01;
  cmd.values = 0x01;
  cmd.deadline_ns = now.value() + 500'000'000LL;
  RCR_REQUIRE(daemon.publish_output_command(cmd).ok());

  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  RCR_EXPECT(daemon.snapshot().io.frames_sent >= 1);

  daemon.request_stop();
  const auto code = daemon.wait_and_stop();
  RCR_EXPECT(code == rcr::DaemonExitCode::Ok);
  sim.stop();
}

RCR_TEST(DaemonCommLossOnHeartbeatStop) {
  require_vcan_or_skip();

  ChildProcess sim;
  RCR_REQUIRE(sim.start(find_node_sim(),
                        {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms", "40",
                         "--duration-ms", "200"}));

  rcr::DaemonConfig cfg{};
  cfg.can_if = "vcan0";
  cfg.node_id = 1;
  cfg.heartbeat_timeout = std::chrono::milliseconds{150};
  cfg.period = std::chrono::milliseconds{10};
  rcr::RuntimeDaemon daemon{cfg};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(daemon.boot().accepted);

  RCR_REQUIRE(wait_until([&] { return daemon.snapshot().node.online; },
                         std::chrono::milliseconds{1000}));

  const bool lost = wait_until(
      [&] {
        const auto snap = daemon.snapshot();
        return snap.node.comm_loss_latched ||
               snap.runtime.fault == rcr::FaultCode::CommLoss;
      },
      std::chrono::milliseconds{2000});
  RCR_EXPECT(lost);
  daemon.request_stop();
  RCR_EXPECT(daemon.wait_and_stop() == rcr::DaemonExitCode::Ok);
}

RCR_TEST(DaemonMapsWideApplicationSequenceToCanRing) {
  require_vcan_or_skip();
  ChildProcess sim;
  RCR_REQUIRE(sim.start(find_node_sim(),
                        {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms", "40"}));
  rcr::RuntimeDaemon daemon{{}};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(wait_until([&] { return daemon.snapshot().node.online; },
                         std::chrono::milliseconds{1000}));
  RCR_REQUIRE(wait_until([&] { return daemon.snapshot().runtime.interlock_ready; },
                         std::chrono::milliseconds{1000}));
  RCR_REQUIRE(daemon.activate().accepted);

  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  rcr::OutputCommand cmd{};
  cmd.session_id = daemon.snapshot().node.session_id;
  cmd.sequence = 65'536;
  cmd.mask = 1;
  cmd.values = 1;
  cmd.deadline_ns = now.value() + 500'000'000LL;
  RCR_REQUIRE(daemon.publish_output_command(cmd).ok());
  RCR_REQUIRE(wait_until([&] { return daemon.snapshot().io.frames_sent >= 1; },
                         std::chrono::milliseconds{500}));
  daemon.request_stop();
  RCR_EXPECT(daemon.wait_and_stop() == rcr::DaemonExitCode::Ok);
  sim.stop();
}

RCR_TEST(DaemonRepeatStartStopFdAndThreadStable) {
  require_vcan_or_skip();

  // 同进程重复组装/拆除 RuntimeDaemon：这是抓 fd/线程泄漏的主证据。
  // 子进程退出后内核会回收其 fd，单靠 fork 循环无法证明本进程无泄漏。
  const pid_t self = ::getpid();
  const int fds_before = rcr::test::count_proc_fds(self);
  const int threads_before = rcr::test::count_proc_threads(self);
  RCR_REQUIRE(fds_before > 0);
  RCR_REQUIRE(threads_before > 0);

  constexpr int kIterations = 100;
  for (int i = 0; i < kIterations; ++i) {
    rcr::DaemonConfig cfg{};
    cfg.can_if = "vcan0";
    cfg.node_id = 1;
    cfg.duration = std::chrono::milliseconds{40};
    rcr::RuntimeDaemon daemon{cfg};
    RCR_REQUIRE(daemon.start().ok());
    RCR_EXPECT(daemon.wait_and_stop() == rcr::DaemonExitCode::Ok);

    const int threads_now = rcr::test::count_proc_threads(self);
    const int fds_now = rcr::test::count_proc_fds(self);
    RCR_REQUIRE(threads_now == threads_before);
    RCR_REQUIRE(fds_now == fds_before);
  }
}

RCR_TEST(DaemonVcanInterfaceDownPropagatesIoError) {
  // 阶段 B：运行中把 vcan 接口 down，验证 I/O 以 IoError/SendFailure 有界退出，
  // 并映射为 WorkerFailure。默认 Skip；需 RCR_ALLOW_IFACE_DOWN=1 且具备 CAP_NET_ADMIN。
  require_iface_down_authorization_or_skip();
  require_vcan_or_skip();

  VcanLinkDownGuard link_guard{"vcan0"};

  ChildProcess sim;
  RCR_REQUIRE(sim.start(find_node_sim(),
                        {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms", "40"}));

  rcr::DaemonConfig cfg{};
  cfg.can_if = "vcan0";
  cfg.node_id = 1;
  cfg.period = std::chrono::milliseconds{10};
  cfg.heartbeat_timeout = std::chrono::milliseconds{300};
  rcr::RuntimeDaemon daemon{cfg};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(daemon.boot().accepted);
  RCR_REQUIRE(wait_until([&] { return daemon.snapshot().node.online; },
                         std::chrono::milliseconds{2000}));
  RCR_REQUIRE(wait_until([&] { return daemon.snapshot().runtime.interlock_ready; },
                         std::chrono::milliseconds{2000}));
  RCR_REQUIRE(daemon.activate().accepted);

  if (!link_guard.down()) {
    daemon.request_stop();
    (void)daemon.wait_and_stop();
    sim.stop();
    RCR_SKIP("ip link set vcan0 down failed (need root/CAP_NET_ADMIN)");
  }

  // 强制走发送路径：仅靠 EPOLLERR 在部分内核/vcan 上可能来得慢或不触发。
  const auto now = rcr::monotonic_now_ns();
  RCR_REQUIRE(now.ok());
  rcr::OutputCommand cmd{};
  cmd.session_id = daemon.snapshot().node.session_id;
  cmd.sequence = 1;
  cmd.mask = 0x01;
  cmd.values = 0x01;
  cmd.deadline_ns = now.value() + 500'000'000LL;
  (void)daemon.publish_output_command(cmd);

  // stop_reason 必须在 wait_and_stop() 之前采样：stop 会 io_.reset()，之后 snapshot().io
  // 变回默认 None，会把已经成功的 IO_ERROR 误判成失败。
  rcr::IoStopReason seen_reason = rcr::IoStopReason::None;
  const bool failed_closed = wait_until(
      [&] {
        const auto snap = daemon.snapshot();
        if (snap.io.stop_reason == rcr::IoStopReason::IoError ||
            snap.io.stop_reason == rcr::IoStopReason::SendFailure) {
          seen_reason = snap.io.stop_reason;
          return true;
        }
        return false;
      },
      std::chrono::milliseconds{3000});
  RCR_EXPECT(failed_closed);
  RCR_EXPECT(seen_reason == rcr::IoStopReason::IoError ||
             seen_reason == rcr::IoStopReason::SendFailure);

  const auto code = daemon.wait_and_stop();
  RCR_EXPECT(code == rcr::DaemonExitCode::WorkerFailure);

  sim.stop();
  // guard 析构会 up；这里再确认后续测试仍看得到可用接口。
  RCR_EXPECT(run_ip_link("vcan0", "up") ||
             rcr::probe_can_interface("vcan0") == rcr::CanInterfaceStatus::Available);
}

RCR_TEST_MAIN()
