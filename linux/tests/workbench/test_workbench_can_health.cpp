#include "rcr/workbench/services/can_health_test.hpp"
#include "rcr/workbench/services/result_writer.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using rcr::Result;
using rcr::workbench::CanCommunicationHealthTest;
using rcr::workbench::CanHealthCriteria;
using rcr::workbench::DiagnosticSource;
using rcr::workbench::EvidenceClass;
using rcr::workbench::MeasurementQuality;
using rcr::workbench::ResultWriter;
using rcr::workbench::RuntimeFaultCode;
using rcr::workbench::RuntimeTelemetrySnapshot;
using rcr::workbench::TestOutcome;
using rcr::workbench::TestRunner;

constexpr std::int64_t kMillisecondNs = 1'000'000;

struct FakeObservation {
  std::int64_t now_ns{0};
  bool runtime_started{true};
  bool scheduler_running{true};
  bool publish_heartbeat{true};
  bool device_online{true};
  std::uint64_t initial_heartbeats{0};
  std::uint64_t decode_rejects{0};
  EvidenceClass evidence{EvidenceClass::Vcan};

  RuntimeTelemetrySnapshot snapshot() const {
    RuntimeTelemetrySnapshot value{};
    value.observed_monotonic_ns = now_ns;
    value.runtime.started = runtime_started;
    value.runtime.scheduler_running = scheduler_running;
    value.runtime.fault = RuntimeFaultCode::None;
    value.communication.evidence = evidence;
    value.communication.decode_rejects = decode_rejects;
    value.device.ever_seen = publish_heartbeat;
    value.device.online = publish_heartbeat && device_online;
    value.device.heartbeats =
        initial_heartbeats +
        (publish_heartbeat
             ? static_cast<std::uint64_t>(now_ns / (20 * kMillisecondNs))
             : 0);
    value.device.heartbeat_age_ns = publish_heartbeat ? 5 * kMillisecondNs : -1;
    return value;
  }
};

CanHealthCriteria criteria() {
  CanHealthCriteria value{};
  value.expected_evidence = EvidenceClass::Vcan;
  value.observation_window = std::chrono::milliseconds{100};
  value.sample_interval = std::chrono::milliseconds{20};
  value.test_timeout = std::chrono::milliseconds{200};
  value.min_heartbeat_delta = 3;
  value.max_heartbeat_age = std::chrono::milliseconds{40};
  return value;
}

CanCommunicationHealthTest make_test(FakeObservation &observation) {
  return CanCommunicationHealthTest{
      [&observation] { return observation.snapshot(); },
      [&observation](std::chrono::nanoseconds duration) {
        observation.now_ns += duration.count();
        return Result<void>::success();
      }};
}

const rcr::workbench::CriterionResult *
find_criterion(const rcr::workbench::TestResult &result,
               const char *criterion_name) {
  for (const auto &item : result.criteria) {
    if (item.name == criterion_name) {
      return &item;
    }
  }
  return nullptr;
}

const rcr::workbench::DiagnosticEvent *
find_diagnostic(const rcr::workbench::TestResult &result, const char *code) {
  for (const auto &item : result.diagnostics) {
    if (item.code == code) {
      return &item;
    }
  }
  return nullptr;
}

class TempDir {
public:
  TempDir() {
    auto pattern =
        (std::filesystem::temp_directory_path() / "rcr-wb-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) == nullptr) {
      throw rcr::test::Failure("mkdtemp failed");
    }
    path_ = buffer.data();
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_{};
};

} // namespace

RCR_TEST(HealthyVcanWindowPassesWithSimulatedEvidence) {
  FakeObservation observation{};
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  auto health = make_test(observation);

  const auto result = health.run(runner, "can-health-pass", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Passed);
  RCR_EXPECT(result.measurements.size() == 6);
  RCR_EXPECT(result.criteria.size() == 8);
  RCR_EXPECT(!result.measurements.empty());
  RCR_EXPECT(result.measurements.front().quality ==
             MeasurementQuality::Simulated);
  const auto *heartbeat = find_criterion(result, "heartbeat progress");
  RCR_REQUIRE(heartbeat != nullptr);
  RCR_EXPECT(heartbeat->passed);
}

RCR_TEST(MissingHeartbeatIsAHealthFailureNotRunnerError) {
  FakeObservation observation{};
  observation.publish_heartbeat = false;
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  auto health = make_test(observation);

  const auto result = health.run(runner, "can-health-no-heartbeat", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Failed);
  RCR_EXPECT(!result.error);
  const auto *progress = find_criterion(result, "heartbeat progress");
  const auto *freshness = find_criterion(result, "heartbeat freshness");
  const auto *online = find_criterion(result, "device remained online");
  RCR_REQUIRE(progress != nullptr);
  RCR_REQUIRE(freshness != nullptr);
  RCR_REQUIRE(online != nullptr);
  RCR_EXPECT(!progress->passed);
  RCR_EXPECT(!freshness->passed);
  RCR_EXPECT(!online->passed);
  RCR_EXPECT(find_diagnostic(result, "HEARTBEAT_STALLED") != nullptr);
  RCR_EXPECT(find_diagnostic(result, "CRITERION_FAILED") != nullptr);
}

RCR_TEST(NewDecodeRejectFailsConfiguredCriterion) {
  FakeObservation observation{};
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  CanCommunicationHealthTest health{
      [&observation] {
        auto snapshot = observation.snapshot();
        snapshot.communication.decode_rejects =
            observation.now_ns >= 40 * kMillisecondNs ? 1 : 0;
        return snapshot;
      },
      [&observation](std::chrono::nanoseconds duration) {
        observation.now_ns += duration.count();
        return Result<void>::success();
      }};

  const auto result =
      health.run(runner, "can-health-decode-reject", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Failed);
  const auto *decode = find_criterion(result, "decode rejects");
  RCR_REQUIRE(decode != nullptr);
  RCR_EXPECT(!decode->passed);
  RCR_EXPECT(decode->actual == "1");
  RCR_EXPECT(find_diagnostic(result, "MALFORMED_FRAME") != nullptr);
}

RCR_TEST(RuntimeMustAlreadyBeRunning) {
  FakeObservation observation{};
  observation.runtime_started = false;
  observation.scheduler_running = false;
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  auto health = make_test(observation);

  const auto result = health.run(runner, "can-health-not-open", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::NotOpen);
}

RCR_TEST(EvidenceClassMustMatchExplicitExpectation) {
  FakeObservation observation{};
  observation.evidence = EvidenceClass::Mock;
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  auto health = make_test(observation);

  const auto result =
      health.run(runner, "can-health-wrong-evidence", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::Rejected);
}

RCR_TEST(CounterRegressionInvalidatesObservationWindow) {
  FakeObservation observation{};
  observation.initial_heartbeats = 10;
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  CanCommunicationHealthTest health{
      [&observation] {
        auto snapshot = observation.snapshot();
        if (observation.now_ns > 0) {
          snapshot.device.heartbeats = 9;
        }
        return snapshot;
      },
      [&observation](std::chrono::nanoseconds duration) {
        observation.now_ns += duration.count();
        return Result<void>::success();
      }};

  const auto result =
      health.run(runner, "can-health-counter-reset", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::Rejected);
}

RCR_TEST(CancelDuringObservationAbortsAndCleansUp) {
  FakeObservation observation{};
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  CanCommunicationHealthTest health{
      [&observation] { return observation.snapshot(); },
      [&observation, &runner](std::chrono::nanoseconds duration) {
        observation.now_ns += duration.count();
        runner.request_cancel();
        return Result<void>::success();
      }};

  const auto result = health.run(runner, "can-health-cancel", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Aborted);
  RCR_EXPECT(result.cleanup_status == rcr::workbench::CleanupStatus::Passed);
}

RCR_TEST(StaleHeartbeatFailsFreshnessAndRecordsCommunicationDiagnostic) {
  FakeObservation observation{};
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  CanCommunicationHealthTest health{
      [&observation] {
        auto snapshot = observation.snapshot();
        snapshot.device.heartbeat_age_ns = 80 * kMillisecondNs;
        return snapshot;
      },
      [&observation](std::chrono::nanoseconds duration) {
        observation.now_ns += duration.count();
        return Result<void>::success();
      }};

  const auto result =
      health.run(runner, "can-health-stale-heartbeat", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Failed);
  const auto *freshness = find_criterion(result, "heartbeat freshness");
  RCR_REQUIRE(freshness != nullptr);
  RCR_EXPECT(!freshness->passed);
  RCR_EXPECT(find_diagnostic(result, "HEARTBEAT_STALE") != nullptr);
  RCR_EXPECT(find_diagnostic(result, "HEARTBEAT_STALE")->source ==
             DiagnosticSource::Communication);
}

RCR_TEST(ObservationTimeoutIsErrorWithSealedEvidence) {
  FakeObservation observation{};
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  CanCommunicationHealthTest health{
      [&observation] { return observation.snapshot(); },
      [&observation](std::chrono::nanoseconds) {
        observation.now_ns += 1000 * kMillisecondNs;
        return Result<void>::success();
      }};

  const auto result = health.run(runner, "can-health-timeout", criteria());

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::Timeout);
  RCR_EXPECT(!result.reason.empty());
  RCR_EXPECT(!result.criteria.empty());
  RCR_EXPECT(!result.measurements.empty());
  RCR_EXPECT(!result.diagnostics.empty());
  RCR_EXPECT(result.cleanup_status == rcr::workbench::CleanupStatus::Passed);
}

RCR_TEST(FailedHealthResultPersistsJsonAndCsvEvidence) {
  FakeObservation observation{};
  observation.publish_heartbeat = false;
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  auto health = make_test(observation);
  auto result = health.run(runner, "can-health-persist-fail", criteria());
  result.provenance.git_commit = "testsha";
  result.provenance.git_dirty = true;
  result.provenance.build_type = "Debug";

  TempDir dir;
  ResultWriter writer;
  const auto written = writer.write(result, dir.path());
  RCR_REQUIRE(written.ok());

  std::ifstream json_in(written.value().json_path);
  std::string json((std::istreambuf_iterator<char>(json_in)),
                   std::istreambuf_iterator<char>());
  RCR_EXPECT(json.find("\"schema\": \"rcr.workbench.result.v1\"") !=
             std::string::npos);
  RCR_EXPECT(json.find("\"outcome\": \"FAIL\"") != std::string::npos);
  RCR_EXPECT(json.find("\"reason\"") != std::string::npos);
  RCR_EXPECT(json.find("heartbeat progress") != std::string::npos);
  RCR_EXPECT(json.find("HEARTBEAT_STALLED") != std::string::npos);
  RCR_EXPECT(json.find("\"status\": \"PASSED\"") != std::string::npos);

  std::ifstream csv_in(written.value().csv_path);
  std::string header;
  std::string row;
  RCR_REQUIRE(static_cast<bool>(std::getline(csv_in, header)));
  RCR_REQUIRE(static_cast<bool>(std::getline(csv_in, row)));
  RCR_EXPECT(header.find("run_id") != std::string::npos);
  RCR_EXPECT(row.find("FAIL") != std::string::npos);
  RCR_EXPECT(row.find("can-health-persist-fail") != std::string::npos);
}

RCR_TEST(InvalidTimingCriteriaFailBeforeSampling) {
  FakeObservation observation{};
  TestRunner runner{
      [&observation] { return Result<std::int64_t>{observation.now_ns}; }};
  auto health = make_test(observation);
  auto invalid = criteria();
  invalid.test_timeout = invalid.observation_window;

  const auto result = health.run(runner, "can-health-invalid", invalid);

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::InvalidArgument);
}

RCR_TEST_MAIN()
