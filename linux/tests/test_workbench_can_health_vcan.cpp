// 可选 vcan 纵向切片：Workbench -> Adapter -> RuntimeDaemon -> SocketCAN ->
// rcr_node_sim。vcan 结果仍是模拟证据，不代表物理 CAN。
#include "rcr/can_bus.hpp"
#include "rcr/runtime_daemon.hpp"
#include "rcr/vcan.hpp"
#include "rcr/workbench/can_health_test.hpp"
#include "rcr/workbench/result_writer.hpp"
#include "rcr/workbench/runtime_application_adapter.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdlib>
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

void persist_if_requested(rcr::workbench::TestResult result) {
  const char *directory = std::getenv("RCR_WORKBENCH_RESULT_DIR");
  if (directory == nullptr || directory[0] == '\0') {
    return;
  }

  const char *commit = std::getenv("RCR_WORKBENCH_GIT_COMMIT");
  const char *build_type = std::getenv("RCR_WORKBENCH_BUILD_TYPE");
  result.provenance.git_commit =
      commit != nullptr && commit[0] != '\0' ? commit : "unknown";
  result.provenance.git_dirty = false;
  result.provenance.build_type =
      build_type != nullptr && build_type[0] != '\0' ? build_type : "unknown";

  rcr::workbench::ResultWriter writer;
  const auto written = writer.write(result, directory);
  RCR_REQUIRE(written.ok());
}

class ChildProcess {
public:
  ~ChildProcess() { stop(); }

  bool start(const std::string &path, const std::vector<std::string> &args) {
    const pid_t child = ::fork();
    if (child < 0) {
      return false;
    }
    if (child == 0) {
      std::vector<char *> argv;
      argv.push_back(const_cast<char *>(path.c_str()));
      for (const auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
      }
      argv.push_back(nullptr);
      ::execv(path.c_str(), argv.data());
      std::_Exit(127);
    }
    pid_ = child;
    return true;
  }

  void stop() {
    if (pid_ <= 0) {
      return;
    }
    ::kill(pid_, SIGTERM);
    int status = 0;
    for (int attempt = 0; attempt < 50; ++attempt) {
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
  rcr::SocketCan probe{"vcan0"};
  const auto opened = probe.open();
  if (!opened) {
    RCR_SKIP(std::string{"cannot open vcan0: "} + opened.error().message());
  }
}

} // namespace

RCR_TEST(RuntimeConnectedCanHealthPassesAgainstVcanNodeSimulator) {
  require_vcan_or_skip();

  ChildProcess node;
  RCR_REQUIRE(node.start(RCR_NODE_SIM_PATH, {"--can", "vcan0", "--node-id", "1",
                                             "--heartbeat-ms", "20"}));

  rcr::DaemonConfig config{};
  config.can_if = "vcan0";
  config.node_id = 1;
  config.period = std::chrono::milliseconds{10};
  config.heartbeat_timeout = std::chrono::milliseconds{200};
  rcr::RuntimeDaemon daemon{config};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(daemon.boot().accepted);

  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  rcr::workbench::CanCommunicationHealthTest health{adapter};
  rcr::workbench::TestRunner runner;
  rcr::workbench::CanHealthCriteria criteria{};
  criteria.expected_evidence = rcr::workbench::EvidenceClass::Vcan;
  criteria.observation_window = std::chrono::milliseconds{300};
  criteria.sample_interval = std::chrono::milliseconds{20};
  criteria.test_timeout = std::chrono::milliseconds{800};
  criteria.min_heartbeat_delta = 5;
  criteria.max_heartbeat_age = std::chrono::milliseconds{100};

  const auto result = health.run(runner, "vcan-health-e2e", criteria);

  RCR_EXPECT(result.outcome == rcr::workbench::TestOutcome::Passed);
  RCR_EXPECT(result.cleanup_status == rcr::workbench::CleanupStatus::Passed);
  persist_if_requested(result);
  daemon.stop();
}

RCR_TEST(StoppedHeartbeatInjectionFailsHealthOnVcan) {
  require_vcan_or_skip();

  ChildProcess node;
  RCR_REQUIRE(node.start(RCR_NODE_SIM_PATH,
                         {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms",
                          "20", "--fault-stop-heartbeat"}));

  rcr::DaemonConfig config{};
  config.can_if = "vcan0";
  config.node_id = 1;
  config.period = std::chrono::milliseconds{10};
  config.heartbeat_timeout = std::chrono::milliseconds{200};
  rcr::RuntimeDaemon daemon{config};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(daemon.boot().accepted);

  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  rcr::workbench::CanCommunicationHealthTest health{adapter};
  rcr::workbench::TestRunner runner;
  rcr::workbench::CanHealthCriteria criteria{};
  criteria.expected_evidence = rcr::workbench::EvidenceClass::Vcan;
  criteria.observation_window = std::chrono::milliseconds{200};
  criteria.sample_interval = std::chrono::milliseconds{20};
  criteria.test_timeout = std::chrono::milliseconds{600};
  criteria.min_heartbeat_delta = 3;
  criteria.max_heartbeat_age = std::chrono::milliseconds{80};

  const auto result = health.run(runner, "vcan-health-stop-hb", criteria);

  RCR_EXPECT(result.outcome == rcr::workbench::TestOutcome::Failed);
  RCR_EXPECT(!result.reason.empty());
  RCR_EXPECT(!result.criteria.empty());
  RCR_EXPECT(!result.diagnostics.empty());
  RCR_EXPECT(result.cleanup_status == rcr::workbench::CleanupStatus::Passed);
  persist_if_requested(result);
  daemon.stop();
}

RCR_TEST(IllegalFrameInjectionFailsDecodeCriterionOnVcan) {
  require_vcan_or_skip();

  ChildProcess node;
  RCR_REQUIRE(node.start(RCR_NODE_SIM_PATH,
                         {"--can", "vcan0", "--node-id", "1", "--heartbeat-ms",
                          "20", "--fault-send-illegal-after-ms", "40"}));

  rcr::DaemonConfig config{};
  config.can_if = "vcan0";
  config.node_id = 1;
  config.period = std::chrono::milliseconds{10};
  config.heartbeat_timeout = std::chrono::milliseconds{200};
  rcr::RuntimeDaemon daemon{config};
  RCR_REQUIRE(daemon.start().ok());
  RCR_REQUIRE(daemon.boot().accepted);

  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  rcr::workbench::CanCommunicationHealthTest health{adapter};
  rcr::workbench::TestRunner runner;
  rcr::workbench::CanHealthCriteria criteria{};
  criteria.expected_evidence = rcr::workbench::EvidenceClass::Vcan;
  criteria.observation_window = std::chrono::milliseconds{300};
  criteria.sample_interval = std::chrono::milliseconds{20};
  criteria.test_timeout = std::chrono::milliseconds{800};
  criteria.min_heartbeat_delta = 3;
  criteria.max_heartbeat_age = std::chrono::milliseconds{120};

  const auto result = health.run(runner, "vcan-health-illegal", criteria);

  RCR_EXPECT(result.outcome == rcr::workbench::TestOutcome::Failed);
  bool saw_decode_fail = false;
  for (const auto &item : result.criteria) {
    if (item.name == "decode rejects" && !item.passed) {
      saw_decode_fail = true;
    }
  }
  RCR_EXPECT(saw_decode_fail);
  bool saw_malformed = false;
  for (const auto &item : result.diagnostics) {
    if (item.code == "MALFORMED_FRAME") {
      saw_malformed = true;
    }
  }
  RCR_EXPECT(saw_malformed);
  persist_if_requested(result);
  daemon.stop();
}

RCR_TEST_MAIN()
