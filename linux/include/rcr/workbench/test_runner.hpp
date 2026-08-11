#pragma once

// Workbench 应用层：测试生命周期与判定合同；不属于 Runtime 控制权威或 MCU
// 线协议。

#include "rcr/result.hpp"
#include "rcr/workbench/application_model.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rcr::workbench {

enum class TestRunStage : std::uint8_t {
  Idle = 0,
  Preparing,
  Executing,
  Evaluating,
  CleaningUp,
  Completed,
};

enum class TestOutcome : std::uint8_t {
  Passed = 0,
  Failed,
  Error,
  Aborted,
  Skipped,
};

enum class CleanupStatus : std::uint8_t {
  NotRun = 0,
  Passed,
  Failed,
};

enum class MeasurementQuality : std::uint8_t {
  Valid = 0,
  Simulated,
  Invalid,
};

[[nodiscard]] constexpr std::string_view
to_string(TestRunStage stage) noexcept {
  switch (stage) {
  case TestRunStage::Idle:
    return "IDLE";
  case TestRunStage::Preparing:
    return "PREPARING";
  case TestRunStage::Executing:
    return "EXECUTING";
  case TestRunStage::Evaluating:
    return "EVALUATING";
  case TestRunStage::CleaningUp:
    return "CLEANING_UP";
  case TestRunStage::Completed:
    return "COMPLETED";
  }
  return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view
to_string(TestOutcome outcome) noexcept {
  switch (outcome) {
  case TestOutcome::Passed:
    return "PASS";
  case TestOutcome::Failed:
    return "FAIL";
  case TestOutcome::Error:
    return "ERROR";
  case TestOutcome::Aborted:
    return "ABORTED";
  case TestOutcome::Skipped:
    return "SKIPPED";
  }
  return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view
to_string(CleanupStatus status) noexcept {
  switch (status) {
  case CleanupStatus::NotRun:
    return "NOT_RUN";
  case CleanupStatus::Passed:
    return "PASSED";
  case CleanupStatus::Failed:
    return "FAILED";
  }
  return "UNKNOWN";
}

[[nodiscard]] constexpr std::string_view
to_string(MeasurementQuality quality) noexcept {
  switch (quality) {
  case MeasurementQuality::Valid:
    return "VALID";
  case MeasurementQuality::Simulated:
    return "SIMULATED";
  case MeasurementQuality::Invalid:
    return "INVALID";
  }
  return "UNKNOWN";
}

struct Measurement {
  std::string name{};
  std::string unit{};
  double value{0.0};
  std::int64_t monotonic_ns{0};
  MeasurementQuality quality{MeasurementQuality::Valid};
};

struct CriterionResult {
  std::string name{};
  bool passed{false};
  std::string expected{};
  std::string actual{};
};

struct TestEvaluation {
  bool passed{false};
  std::string summary{};
  std::vector<CriterionResult> criteria{};
};

struct TestParameter {
  std::string name{};
  std::string value{};
};

struct TestRunEnvironment {
  std::string backend{};
  std::string interface_name{};
  std::string dut_id{};
  std::string profile{};
  EvidenceClass evidence{EvidenceClass::Unspecified};
};

struct TestRunProvenance {
  // 未知时按 dirty 处理，避免把来源不明的二进制写成干净 commit 证据。
  std::string git_commit{"unknown"};
  bool git_dirty{true};
  std::string build_type{"unknown"};
};

struct TestResult {
  std::string run_id{};
  std::string case_id{};
  std::string case_name{};
  std::string case_version{"1"};
  TestOutcome outcome{TestOutcome::Error};
  std::int64_t started_ns{0};
  std::int64_t finished_ns{0};
  std::int64_t started_wall_ns{0};
  std::int64_t finished_wall_ns{0};
  std::vector<Measurement> measurements{};
  std::vector<CriterionResult> criteria{};
  std::vector<DiagnosticEvent> diagnostics{};
  std::vector<TestParameter> parameters{};
  TestRunEnvironment environment{};
  TestRunProvenance provenance{};
  std::string summary{};
  // FAIL/ERROR 的主因；与 summary 分开，避免
  // UI/文件用一段描述同时充当判定和原因。
  std::string reason{};
  Error error{};
  CleanupStatus cleanup_status{CleanupStatus::NotRun};
  Error cleanup_error{};
};

/// 一次 run 的受控上下文。它只在调用 run() 的线程中使用；跨线程取消由内部
/// atomic 标志 传递。长时间等待的 case 必须周期性调用
/// cancellation_requested()/deadline_expired()， 不能只依赖 Runner
/// 在阶段边界检查。
class TestRunContext {
public:
  using Clock = std::function<Result<std::int64_t>()>;

  [[nodiscard]] bool cancellation_requested() const noexcept;
  [[nodiscard]] Result<bool> deadline_expired() const;
  [[nodiscard]] Result<std::int64_t> now_ns() const;
  [[nodiscard]] std::int64_t deadline_ns() const noexcept;
  [[nodiscard]] std::string_view run_id() const noexcept;

  void add_measurement(Measurement measurement);
  void add_diagnostic(DiagnosticEvent event);
  void add_parameter(std::string name, std::string value);
  void set_environment(TestRunEnvironment environment);

private:
  friend class TestRunner;

  TestRunContext(const std::atomic<bool> &cancel_requested, Clock &clock,
                 std::int64_t deadline_ns, TestResult &result);

  const std::atomic<bool> &cancel_requested_;
  Clock &clock_;
  std::int64_t deadline_ns_{0};
  TestResult &result_;
};

/// 固定 C++ test case 合同。当前不引入 DSL；每个 callback 返回显式
/// Result，避免异常跨过 Workbench 生命周期边界。cleanup 即使在
/// prepare/execute/evaluate 失败后也会被调用。
struct TestCaseDefinition {
  using Step = std::function<Result<void>(TestRunContext &)>;
  using Evaluate = std::function<Result<TestEvaluation>(TestRunContext &)>;

  std::string id{};
  std::string name{};
  std::string version{"1"};
  std::chrono::nanoseconds timeout{};
  Step prepare{};
  Step execute{};
  Evaluate evaluate{};
  Step cleanup{};
};

/// 同步 Test Runner。它不创建线程，也不拥有 Qt event
/// loop；调用方决定在哪个线程执行。 同一个实例一次只允许一个
/// run，request_cancel() 可由另一线程调用。
class TestRunner {
public:
  using Clock = TestRunContext::Clock;

  TestRunner();
  explicit TestRunner(Clock clock);

  [[nodiscard]] TestResult run(std::string run_id,
                               const TestCaseDefinition &test_case);
  void request_cancel();

  [[nodiscard]] TestRunStage stage() const noexcept;
  [[nodiscard]] bool running() const noexcept;

private:
  [[nodiscard]] Result<void>
  validate(const std::string &run_id,
           const TestCaseDefinition &test_case) const;
  [[nodiscard]] Result<bool> should_stop(TestRunContext &context) const;

  Clock clock_;
  // 只串行化 run 启动与 cancel 握手；执行 callback 时不持锁，避免阻塞 UI/CLI
  // 的取消请求。
  mutable std::mutex start_cancel_mutex_{};
  std::atomic<bool> cancel_requested_{false};
  std::atomic<bool> running_{false};
  std::atomic<TestRunStage> stage_{TestRunStage::Idle};
};

} // namespace rcr::workbench
