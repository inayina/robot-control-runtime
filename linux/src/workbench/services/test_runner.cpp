#include "rcr/workbench/services/test_runner.hpp"

#include "rcr/time.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rcr::workbench {
namespace {

Error aborted_error() { return Error{Errc::Rejected, "test run cancelled"}; }

Error timeout_error() {
  return Error{Errc::Timeout, "test run deadline expired"};
}

class RunningGuard {
public:
  RunningGuard(std::atomic<bool> &running, std::atomic<TestRunStage> &stage)
      : running_(running), stage_(stage) {}

  ~RunningGuard() {
    stage_.store(TestRunStage::Completed, std::memory_order_release);
    running_.store(false, std::memory_order_release);
  }

  RunningGuard(const RunningGuard &) = delete;
  RunningGuard &operator=(const RunningGuard &) = delete;

private:
  std::atomic<bool> &running_;
  std::atomic<TestRunStage> &stage_;
};

void assign_failure_reason(TestResult &result, std::string fallback) {
  if (!result.reason.empty()) {
    return;
  }
  if (!result.error.message().empty()) {
    result.reason = result.error.message();
    return;
  }
  if (!result.summary.empty()) {
    result.reason = result.summary;
    return;
  }
  result.reason = std::move(fallback);
}

void seal_failure_evidence(TestResult &result) {
  // PASS/ABORTED/SKIPPED 只补 reason；Gate 要求每个 FAIL/ERROR 都能独立复核
  // criteria、measurement、reason 和 cleanup，即使 case 在 Evaluate 前就失败。
  if (result.outcome != TestOutcome::Failed &&
      result.outcome != TestOutcome::Error) {
    if (result.reason.empty()) {
      result.reason = result.summary;
    }
    return;
  }

  assign_failure_reason(result, std::string{to_string(result.outcome)});
  if (result.measurements.empty()) {
    result.measurements.push_back(Measurement{
        "lifecycle_samples", "count", 0.0,
        result.finished_ns != 0 ? result.finished_ns : result.started_ns,
        MeasurementQuality::Invalid});
  }
  if (result.criteria.empty()) {
    result.criteria.push_back(CriterionResult{"evaluation completed", false,
                                              "completed", result.reason});
  }

  const bool has_test_diagnostic =
      std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                  [](const DiagnosticEvent &event) {
                    return event.source == DiagnosticSource::Test;
                  });
  if (!has_test_diagnostic) {
    DiagnosticEvent event{};
    event.observed_monotonic_ns =
        result.finished_ns != 0 ? result.finished_ns : result.started_ns;
    event.source = DiagnosticSource::Test;
    event.severity = DiagnosticSeverity::Error;
    event.code =
        result.outcome == TestOutcome::Failed ? "TEST_FAILED" : "TEST_ERROR";
    event.message = result.reason;
    event.device_id = result.environment.dut_id;
    event.run_id = result.run_id;
    result.diagnostics.push_back(std::move(event));
  }
}

void sample_wall_clock(std::int64_t &destination) {
  const auto wall = realtime_now_ns();
  if (wall) {
    destination = wall.value();
  }
}

} // namespace

TestRunContext::TestRunContext(const std::atomic<bool> &cancel_requested,
                               Clock &clock, std::int64_t deadline_ns,
                               TestResult &result)
    : cancel_requested_(cancel_requested), clock_(clock),
      deadline_ns_(deadline_ns), result_(result) {}

bool TestRunContext::cancellation_requested() const noexcept {
  return cancel_requested_.load(std::memory_order_acquire);
}

Result<bool> TestRunContext::deadline_expired() const {
  auto now = now_ns();
  if (!now) {
    return now.error();
  }
  return now.value() >= deadline_ns_;
}

Result<std::int64_t> TestRunContext::now_ns() const { return clock_(); }

std::int64_t TestRunContext::deadline_ns() const noexcept {
  return deadline_ns_;
}

std::string_view TestRunContext::run_id() const noexcept {
  return result_.run_id;
}

void TestRunContext::add_measurement(Measurement measurement) {
  result_.measurements.push_back(std::move(measurement));
}

void TestRunContext::add_diagnostic(DiagnosticEvent event) {
  if (event.run_id.empty()) {
    event.run_id = result_.run_id;
  }
  result_.diagnostics.push_back(std::move(event));
}

void TestRunContext::add_parameter(std::string name, std::string value) {
  result_.parameters.push_back(
      TestParameter{std::move(name), std::move(value)});
}

void TestRunContext::set_environment(TestRunEnvironment environment) {
  result_.environment = std::move(environment);
}

TestRunner::TestRunner() : TestRunner([] { return monotonic_now_ns(); }) {}

TestRunner::TestRunner(Clock clock) : clock_(std::move(clock)) {}

TestResult TestRunner::run(std::string run_id,
                           const TestCaseDefinition &test_case) {
  TestResult result{};
  result.run_id = std::move(run_id);
  result.case_id = test_case.id;
  result.case_name = test_case.name;
  result.case_version = test_case.version.empty() ? "1" : test_case.version;

  {
    // 与 request_cancel() 共用短临界区，保证“开始新 run + 清旧
    // cancel”不会吞掉并发取消。
    const std::lock_guard lock{start_cancel_mutex_};
    if (running_.load(std::memory_order_acquire)) {
      result.error = Error{Errc::Busy, "test runner already has an active run"};
      result.summary = result.error.message();
      seal_failure_evidence(result);
      return result;
    }
    cancel_requested_.store(false, std::memory_order_release);
    stage_.store(TestRunStage::Idle, std::memory_order_release);
    running_.store(true, std::memory_order_release);
  }

  RunningGuard running_guard{running_, stage_};

  const auto validation = validate(result.run_id, test_case);
  if (!validation) {
    result.error = validation.error();
    result.summary = result.error.message();
    seal_failure_evidence(result);
    return result;
  }

  auto start = clock_();
  if (!start) {
    result.error = start.error();
    result.summary = "failed to sample test start time";
    seal_failure_evidence(result);
    return result;
  }
  result.started_ns = start.value();
  sample_wall_clock(result.started_wall_ns);

  const auto timeout_ns = test_case.timeout.count();
  if (result.started_ns >
      std::numeric_limits<std::int64_t>::max() - timeout_ns) {
    result.error =
        Error{Errc::InvalidArgument, "test deadline overflows int64"};
    result.summary = result.error.message();
    seal_failure_evidence(result);
    return result;
  }

  const std::int64_t deadline_ns = result.started_ns + timeout_ns;
  TestRunContext context{cancel_requested_, clock_, deadline_ns, result};

  auto set_step_error = [&result](const Error &error, std::string summary) {
    result.outcome = TestOutcome::Error;
    result.error = error;
    result.summary = std::move(summary);
    assign_failure_reason(result, result.summary);
  };

  bool continue_run = true;
  stage_.store(TestRunStage::Preparing, std::memory_order_release);
  const auto prepared = test_case.prepare(context);
  if (!prepared) {
    set_step_error(prepared.error(), "prepare failed");
    continue_run = false;
  }

  if (continue_run) {
    auto stop = should_stop(context);
    if (!stop) {
      set_step_error(stop.error(), "failed to check test deadline");
      continue_run = false;
    } else if (stop.value()) {
      if (context.cancellation_requested()) {
        result.outcome = TestOutcome::Aborted;
        result.error = aborted_error();
        result.summary = result.error.message();
        result.reason = result.error.message();
      } else {
        set_step_error(timeout_error(), "test timed out after prepare");
      }
      continue_run = false;
    }
  }

  if (continue_run) {
    stage_.store(TestRunStage::Executing, std::memory_order_release);
    const auto executed = test_case.execute(context);
    if (!executed) {
      set_step_error(executed.error(), "execute failed");
      continue_run = false;
    }
  }

  if (continue_run) {
    auto stop = should_stop(context);
    if (!stop) {
      set_step_error(stop.error(), "failed to check test deadline");
      continue_run = false;
    } else if (stop.value()) {
      if (context.cancellation_requested()) {
        result.outcome = TestOutcome::Aborted;
        result.error = aborted_error();
        result.summary = result.error.message();
        result.reason = result.error.message();
      } else {
        set_step_error(timeout_error(), "test timed out after execute");
      }
      continue_run = false;
    }
  }

  if (continue_run) {
    stage_.store(TestRunStage::Evaluating, std::memory_order_release);
    auto evaluated = test_case.evaluate(context);
    if (!evaluated) {
      set_step_error(evaluated.error(), "evaluation failed");
    } else {
      auto stop = should_stop(context);
      if (!stop) {
        set_step_error(stop.error(), "failed to check test deadline");
      } else if (stop.value()) {
        if (context.cancellation_requested()) {
          result.outcome = TestOutcome::Aborted;
          result.error = aborted_error();
          result.summary = result.error.message();
          result.reason = result.error.message();
        } else {
          set_step_error(timeout_error(), "test timed out during evaluation");
        }
      } else {
        auto evaluation = std::move(evaluated).value();
        result.criteria = std::move(evaluation.criteria);
        result.summary = std::move(evaluation.summary);
        result.outcome =
            evaluation.passed ? TestOutcome::Passed : TestOutcome::Failed;
        result.reason = result.summary;
      }
    }
  }

  // prepare 可能只完成了一部分资源获取，因此从进入 Prepare 起，无论哪一步失败都
  // cleanup。
  stage_.store(TestRunStage::CleaningUp, std::memory_order_release);
  const auto cleaned = test_case.cleanup(context);
  if (cleaned) {
    result.cleanup_status = CleanupStatus::Passed;
  } else {
    result.cleanup_status = CleanupStatus::Failed;
    result.cleanup_error = cleaned.error();
    if (result.outcome == TestOutcome::Passed) {
      result.outcome = TestOutcome::Error;
      result.error = cleaned.error();
      result.summary = "cleanup failed after a passing evaluation";
      result.reason = cleaned.error().message().empty()
                          ? result.summary
                          : cleaned.error().message();
    }
  }

  auto finish = clock_();
  if (finish) {
    result.finished_ns = finish.value();
  } else if (result.outcome == TestOutcome::Passed) {
    result.outcome = TestOutcome::Error;
    result.error = finish.error();
    result.summary = "failed to sample test finish time";
    assign_failure_reason(result, result.summary);
  }
  sample_wall_clock(result.finished_wall_ns);
  seal_failure_evidence(result);
  return result;
}

void TestRunner::request_cancel() {
  const std::lock_guard lock{start_cancel_mutex_};
  if (running_.load(std::memory_order_acquire)) {
    cancel_requested_.store(true, std::memory_order_release);
  }
}

TestRunStage TestRunner::stage() const noexcept {
  return stage_.load(std::memory_order_acquire);
}

bool TestRunner::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

Result<void> TestRunner::validate(const std::string &run_id,
                                  const TestCaseDefinition &test_case) const {
  if (run_id.empty() || test_case.id.empty() || test_case.name.empty()) {
    return Error{Errc::InvalidArgument,
                 "run id, case id and case name must be non-empty"};
  }
  if (test_case.timeout.count() <= 0) {
    return Error{Errc::InvalidArgument, "test timeout must be positive"};
  }
  if (!test_case.prepare || !test_case.execute || !test_case.evaluate ||
      !test_case.cleanup) {
    return Error{Errc::InvalidArgument,
                 "all test lifecycle callbacks are required"};
  }
  return Result<void>::success();
}

Result<bool> TestRunner::should_stop(TestRunContext &context) const {
  if (context.cancellation_requested()) {
    return true;
  }
  return context.deadline_expired();
}

} // namespace rcr::workbench
