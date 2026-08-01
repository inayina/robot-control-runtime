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

RCR_TEST(DaemonRepeatStartStop) {
  require_vcan_or_skip();
  for (int i = 0; i < 20; ++i) {
    rcr::DaemonConfig cfg{};
    cfg.can_if = "vcan0";
    cfg.node_id = 1;
    cfg.duration = std::chrono::milliseconds{50};
    rcr::RuntimeDaemon daemon{cfg};
    RCR_REQUIRE(daemon.start().ok());
    RCR_EXPECT(daemon.wait_and_stop() == rcr::DaemonExitCode::Ok);
  }
}

RCR_TEST_MAIN()
