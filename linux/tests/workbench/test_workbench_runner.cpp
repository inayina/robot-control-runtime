#include "rcr/workbench/services/test_runner.hpp"
#include "test_support.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using rcr::Result;
using rcr::workbench::CleanupStatus;
using rcr::workbench::CriterionResult;
using rcr::workbench::Measurement;
using rcr::workbench::TestCaseDefinition;
using rcr::workbench::TestEvaluation;
using rcr::workbench::TestOutcome;
using rcr::workbench::TestRunContext;
using rcr::workbench::TestRunner;
using rcr::workbench::TestRunStage;

struct FakeClock {
  std::int64_t now_ns{100};

  Result<std::int64_t> now() const { return now_ns; }
};

TestCaseDefinition passing_case(std::vector<std::string> &calls) {
  TestCaseDefinition test_case{};
  test_case.id = "runner.pass";
  test_case.name = "Runner passing lifecycle";
  test_case.timeout = std::chrono::nanoseconds{100};
  test_case.prepare = [&calls](TestRunContext &) {
    calls.emplace_back("prepare");
    return Result<void>::success();
  };
  test_case.execute = [&calls](TestRunContext &context) {
    calls.emplace_back("execute");
    context.add_measurement(Measurement{"heartbeat_age", "ms", 4.0, 110});
    return Result<void>::success();
  };
  test_case.evaluate = [&calls](TestRunContext &) -> Result<TestEvaluation> {
    calls.emplace_back("evaluate");
    TestEvaluation evaluation{};
    evaluation.passed = true;
    evaluation.summary = "criteria passed";
    evaluation.criteria.push_back(
        CriterionResult{"heartbeat age", true, "<=10ms", "4ms"});
    return evaluation;
  };
  test_case.cleanup = [&calls](TestRunContext &) {
    calls.emplace_back("cleanup");
    return Result<void>::success();
  };
  return test_case;
}

} // namespace

RCR_TEST(PassingRunExecutesEveryStageAndCollectsEvidence) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);

  const auto result = runner.run("run-001", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Passed);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Passed);
  RCR_EXPECT(result.started_ns == 100);
  RCR_EXPECT(result.finished_ns == 100);
  RCR_EXPECT(result.measurements.size() == 1);
  RCR_EXPECT(result.criteria.size() == 1);
  RCR_EXPECT(calls == std::vector<std::string>(
                          {"prepare", "execute", "evaluate", "cleanup"}));
  RCR_EXPECT(!runner.running());
  RCR_EXPECT(runner.stage() == TestRunStage::Completed);
}

RCR_TEST(FailedCriterionStillRunsCleanup) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  test_case.evaluate = [&calls](TestRunContext &) -> Result<TestEvaluation> {
    calls.emplace_back("evaluate");
    return TestEvaluation{
        false,
        "heartbeat too old",
        {CriterionResult{"heartbeat age", false, "<=10ms", "30ms"}}};
  };

  const auto result = runner.run("run-002", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Failed);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Passed);
  RCR_EXPECT(result.summary == "heartbeat too old");
  RCR_EXPECT(calls.back() == "cleanup");
}

RCR_TEST(PrepareFailureStillRunsCleanup) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  test_case.prepare = [&calls](TestRunContext &) -> Result<void> {
    calls.emplace_back("prepare");
    return rcr::Error{rcr::Errc::IoError, "partial acquisition failed"};
  };

  const auto result = runner.run("run-003", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::IoError);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Passed);
  RCR_EXPECT(result.reason == "partial acquisition failed");
  RCR_EXPECT(!result.criteria.empty());
  RCR_EXPECT(!result.measurements.empty());
  RCR_EXPECT(!result.diagnostics.empty());
  RCR_EXPECT(calls == std::vector<std::string>({"prepare", "cleanup"}));
}

RCR_TEST(CancelRequestAbortsBeforeEvaluationAndRunsCleanup) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  test_case.execute = [&calls, &runner](TestRunContext &) {
    calls.emplace_back("execute");
    runner.request_cancel();
    return Result<void>::success();
  };

  const auto result = runner.run("run-004", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Aborted);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Passed);
  RCR_EXPECT(result.error.code() == rcr::Errc::Rejected);
  RCR_EXPECT(calls ==
             std::vector<std::string>({"prepare", "execute", "cleanup"}));
}

RCR_TEST(DeadlineExpiryStopsBeforeEvaluationAndRunsCleanup) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  test_case.timeout = std::chrono::nanoseconds{10};
  test_case.execute = [&calls, &clock](TestRunContext &) {
    calls.emplace_back("execute");
    clock.now_ns = 110;
    return Result<void>::success();
  };

  const auto result = runner.run("run-005", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::Timeout);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Passed);
  RCR_EXPECT(calls ==
             std::vector<std::string>({"prepare", "execute", "cleanup"}));
}

RCR_TEST(DeadlineExpiryDuringEvaluationCannotReportPass) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  test_case.timeout = std::chrono::nanoseconds{10};
  test_case.evaluate = [&calls,
                        &clock](TestRunContext &) -> Result<TestEvaluation> {
    calls.emplace_back("evaluate");
    clock.now_ns = 110;
    return TestEvaluation{true, "stale pass", {}};
  };

  const auto result = runner.run("run-005b", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::Timeout);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Passed);
  RCR_EXPECT(calls.back() == "cleanup");
}

RCR_TEST(CleanupFailureCannotLeavePassingOutcome) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  test_case.cleanup = [&calls](TestRunContext &) -> Result<void> {
    calls.emplace_back("cleanup");
    return rcr::Error{rcr::Errc::IoError, "release failed"};
  };

  const auto result = runner.run("run-006", test_case);

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::Failed);
  RCR_EXPECT(result.cleanup_error.code() == rcr::Errc::IoError);
  RCR_EXPECT(result.error.code() == rcr::Errc::IoError);
}

RCR_TEST(InvalidDefinitionDoesNotEnterLifecycle) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  TestCaseDefinition invalid{};

  const auto result = runner.run("", invalid);

  RCR_EXPECT(result.outcome == TestOutcome::Error);
  RCR_EXPECT(result.error.code() == rcr::Errc::InvalidArgument);
  RCR_EXPECT(result.cleanup_status == CleanupStatus::NotRun);
  RCR_EXPECT(!result.reason.empty());
  RCR_EXPECT(!result.criteria.empty());
  RCR_EXPECT(!result.measurements.empty());
  RCR_EXPECT(!result.diagnostics.empty());
  RCR_EXPECT(!runner.running());
}

RCR_TEST(ConcurrentRunIsBusyAndCancelReachesActiveRun) {
  FakeClock clock{};
  TestRunner runner{[&clock] { return clock.now(); }};
  std::vector<std::string> calls;
  auto test_case = passing_case(calls);
  std::mutex mutex;
  std::condition_variable cv;
  bool executing = false;
  bool release = false;
  test_case.execute = [&](TestRunContext &) {
    {
      const std::lock_guard lock{mutex};
      executing = true;
    }
    cv.notify_one();
    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return release; });
    return Result<void>::success();
  };

  rcr::workbench::TestResult first_result{};
  std::thread worker{
      [&] { first_result = runner.run("run-active", test_case); }};
  {
    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return executing; });
  }

  const auto busy_result = runner.run("run-busy", test_case);
  RCR_EXPECT(busy_result.outcome == TestOutcome::Error);
  RCR_EXPECT(busy_result.error.code() == rcr::Errc::Busy);
  runner.request_cancel();
  {
    const std::lock_guard lock{mutex};
    release = true;
  }
  cv.notify_one();
  worker.join();

  RCR_EXPECT(first_result.outcome == TestOutcome::Aborted);
  RCR_EXPECT(first_result.cleanup_status == CleanupStatus::Passed);
}

RCR_TEST_MAIN()
