#include "rcr/workbench/can_health_test.hpp"

#include "rcr/workbench/runtime_application_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

namespace rcr::workbench {
namespace {

struct ObservationState {
  RuntimeTelemetrySnapshot baseline{};
  RuntimeTelemetrySnapshot latest{};
  std::uint64_t samples{0};
  std::int64_t max_heartbeat_age_ns{-1};
  bool offline_after_seen{false};
};

MeasurementQuality quality_for(EvidenceClass evidence) noexcept {
  switch (evidence) {
  case EvidenceClass::Physical:
    return MeasurementQuality::Valid;
  case EvidenceClass::Mock:
  case EvidenceClass::Vcan:
    return MeasurementQuality::Simulated;
  case EvidenceClass::Unspecified:
    return MeasurementQuality::Invalid;
  }
  return MeasurementQuality::Invalid;
}

Result<void> validate_criteria(const CanHealthCriteria &criteria) {
  if (criteria.expected_evidence == EvidenceClass::Unspecified) {
    return Error{Errc::InvalidArgument,
                 "CAN health evidence class must be explicit"};
  }
  if (criteria.observation_window.count() <= 0 ||
      criteria.sample_interval.count() <= 0 ||
      criteria.test_timeout <= criteria.observation_window ||
      criteria.max_heartbeat_age.count() <= 0 ||
      criteria.min_heartbeat_delta == 0) {
    return Error{Errc::InvalidArgument,
                 "CAN health durations and heartbeat threshold are invalid"};
  }
  return Result<void>::success();
}

Result<std::uint64_t> checked_delta(std::uint64_t before, std::uint64_t after,
                                    const char *name) {
  if (after < before) {
    return Error{Errc::Rejected,
                 std::string{name} +
                     " counter moved backwards; Runtime likely restarted"};
  }
  return after - before;
}

CriterionResult criterion(std::string name, bool passed, std::string expected,
                          std::string actual) {
  return CriterionResult{std::move(name), passed, std::move(expected),
                         std::move(actual)};
}

void add_measurement(TestRunContext &context, std::string name,
                     std::string unit, double value, std::int64_t timestamp_ns,
                     MeasurementQuality quality) {
  context.add_measurement(Measurement{std::move(name), std::move(unit), value,
                                      timestamp_ns, quality});
}

void add_diagnostic(TestRunContext &context,
                    const RuntimeTelemetrySnapshot &snap,
                    DiagnosticSource source, DiagnosticSeverity severity,
                    std::string code, std::string message,
                    std::string extra_context = {}) {
  DiagnosticEvent event{};
  event.observed_monotonic_ns = snap.observed_monotonic_ns;
  event.source = source;
  event.severity = severity;
  event.code = std::move(code);
  event.message = std::move(message);
  event.device_id = snap.device.device_id;
  event.run_id = std::string{context.run_id()};
  event.context = std::move(extra_context);
  context.add_diagnostic(std::move(event));
}

void copy_snapshot_diagnostics(TestRunContext &context,
                               const RuntimeTelemetrySnapshot &snap) {
  for (auto event : snap.diagnostics) {
    if (event.run_id.empty()) {
      event.run_id = std::string{context.run_id()};
    }
    if (event.device_id.empty()) {
      event.device_id = snap.device.device_id;
    }
    context.add_diagnostic(std::move(event));
  }
}

void record_parameters(TestRunContext &context,
                       const CanHealthCriteria &criteria) {
  context.add_parameter("observation_window_ms",
                        std::to_string(criteria.observation_window.count()));
  context.add_parameter("sample_interval_ms",
                        std::to_string(criteria.sample_interval.count()));
  context.add_parameter("test_timeout_ms",
                        std::to_string(criteria.test_timeout.count()));
  context.add_parameter("min_heartbeat_delta",
                        std::to_string(criteria.min_heartbeat_delta));
  context.add_parameter("max_heartbeat_age_ms",
                        std::to_string(criteria.max_heartbeat_age.count()));
  context.add_parameter("max_decode_reject_delta",
                        std::to_string(criteria.max_decode_reject_delta));
}

} // namespace

CanCommunicationHealthTest::CanCommunicationHealthTest(
    RuntimeApplicationAdapter &adapter)
    : CanCommunicationHealthTest([&adapter] { return adapter.snapshot(); },
                                 [](std::chrono::nanoseconds duration) {
                                   std::this_thread::sleep_for(duration);
                                   return Result<void>::success();
                                 }) {}

CanCommunicationHealthTest::CanCommunicationHealthTest(
    SnapshotProvider snapshot_provider, Wait wait)
    : snapshot_provider_(std::move(snapshot_provider)), wait_(std::move(wait)) {
}

TestResult CanCommunicationHealthTest::run(TestRunner &runner,
                                           std::string run_id,
                                           CanHealthCriteria criteria) {
  ObservationState observation{};

  TestCaseDefinition test_case{};
  test_case.id = "can.communication_health";
  test_case.name = "CAN Communication Health";
  test_case.version = "1";
  test_case.timeout = criteria.test_timeout;

  test_case.prepare = [this, &criteria,
                       &observation](TestRunContext &context) -> Result<void> {
    const auto valid = validate_criteria(criteria);
    if (!valid) {
      return valid.error();
    }
    if (!snapshot_provider_ || !wait_) {
      return Error{Errc::InvalidArgument,
                   "CAN health provider and wait callback are required"};
    }

    observation.baseline = snapshot_provider_();
    observation.latest = observation.baseline;
    record_parameters(context, criteria);
    context.set_environment(TestRunEnvironment{
        observation.baseline.communication.backend,
        observation.baseline.communication.interface_name,
        observation.baseline.device.device_id, "can.communication_health",
        criteria.expected_evidence});

    if (!observation.baseline.runtime.started ||
        !observation.baseline.runtime.scheduler_running) {
      copy_snapshot_diagnostics(context, observation.baseline);
      add_diagnostic(context, observation.baseline, DiagnosticSource::Test,
                     DiagnosticSeverity::Error, "RUNTIME_NOT_RUNNING",
                     "Runtime and scheduler must be running before CAN health");
      return Error{Errc::NotOpen,
                   "Runtime and scheduler must be running before CAN health"};
    }
    if (observation.baseline.communication.evidence !=
        criteria.expected_evidence) {
      copy_snapshot_diagnostics(context, observation.baseline);
      add_diagnostic(context, observation.baseline, DiagnosticSource::Test,
                     DiagnosticSeverity::Error, "EVIDENCE_MISMATCH",
                     "observed evidence class does not match test criteria",
                     std::string("expected=") +
                         std::string{to_string(criteria.expected_evidence)} +
                         ";actual=" +
                         std::string{to_string(
                             observation.baseline.communication.evidence)});
      return Error{Errc::Rejected,
                   "observed evidence class does not match test criteria"};
    }
    return Result<void>::success();
  };

  test_case.execute = [this, &criteria,
                       &observation](TestRunContext &context) -> Result<void> {
    const auto start = context.now_ns();
    if (!start) {
      return start.error();
    }
    const auto window_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               criteria.observation_window)
                               .count();
    const auto interval_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            criteria.sample_interval)
            .count();

    while (true) {
      if (context.cancellation_requested()) {
        return Result<void>::success();
      }
      const auto expired = context.deadline_expired();
      if (!expired) {
        return expired.error();
      }
      if (expired.value()) {
        return Result<void>::success();
      }

      const auto now = context.now_ns();
      if (!now) {
        return now.error();
      }
      const auto elapsed_ns = now.value() - start.value();
      if (elapsed_ns >= window_ns) {
        break;
      }

      const auto wait_ns = std::min(interval_ns, window_ns - elapsed_ns);
      const auto waited = wait_(std::chrono::nanoseconds{wait_ns});
      if (!waited) {
        return waited.error();
      }

      observation.latest = snapshot_provider_();
      ++observation.samples;
      if (observation.latest.device.heartbeat_age_ns >= 0) {
        observation.max_heartbeat_age_ns =
            std::max(observation.max_heartbeat_age_ns,
                     observation.latest.device.heartbeat_age_ns);
      }
      if (observation.latest.device.ever_seen &&
          !observation.latest.device.online) {
        observation.offline_after_seen = true;
      }
    }

    const auto quality = quality_for(criteria.expected_evidence);
    add_measurement(context, "sample_count", "count",
                    static_cast<double>(observation.samples),
                    observation.latest.observed_monotonic_ns, quality);
    add_measurement(
        context, "max_heartbeat_age", "ms",
        observation.max_heartbeat_age_ns < 0
            ? -1.0
            : static_cast<double>(observation.max_heartbeat_age_ns) /
                  1'000'000.0,
        observation.latest.observed_monotonic_ns, quality);

    const auto heartbeat_delta =
        checked_delta(observation.baseline.device.heartbeats,
                      observation.latest.device.heartbeats, "heartbeat");
    if (!heartbeat_delta) {
      add_diagnostic(context, observation.latest, DiagnosticSource::Test,
                     DiagnosticSeverity::Error, "COUNTER_REGRESSION",
                     heartbeat_delta.error().message());
      return heartbeat_delta.error();
    }
    const auto decode_delta = checked_delta(
        observation.baseline.communication.decode_rejects,
        observation.latest.communication.decode_rejects, "decode reject");
    if (!decode_delta) {
      return decode_delta.error();
    }
    const auto queue_delta = checked_delta(
        observation.baseline.communication.queue_rejects,
        observation.latest.communication.queue_rejects, "queue reject");
    if (!queue_delta) {
      return queue_delta.error();
    }
    const auto drop_delta =
        checked_delta(observation.baseline.communication.input_queue_drop_count,
                      observation.latest.communication.input_queue_drop_count,
                      "input queue drop");
    if (!drop_delta) {
      return drop_delta.error();
    }
    return Result<void>::success();
  };

  test_case.evaluate = [&criteria, &observation](
                           TestRunContext &context) -> Result<TestEvaluation> {
    const auto heartbeat_delta =
        checked_delta(observation.baseline.device.heartbeats,
                      observation.latest.device.heartbeats, "heartbeat");
    const auto decode_delta = checked_delta(
        observation.baseline.communication.decode_rejects,
        observation.latest.communication.decode_rejects, "decode reject");
    const auto queue_delta = checked_delta(
        observation.baseline.communication.queue_rejects,
        observation.latest.communication.queue_rejects, "queue reject");
    const auto drop_delta =
        checked_delta(observation.baseline.communication.input_queue_drop_count,
                      observation.latest.communication.input_queue_drop_count,
                      "input queue drop");
    if (!heartbeat_delta) {
      return heartbeat_delta.error();
    }
    if (!decode_delta) {
      return decode_delta.error();
    }
    if (!queue_delta) {
      return queue_delta.error();
    }
    if (!drop_delta) {
      return drop_delta.error();
    }

    const auto quality = quality_for(criteria.expected_evidence);
    const auto timestamp_ns = observation.latest.observed_monotonic_ns;
    add_measurement(context, "heartbeat_delta", "count",
                    static_cast<double>(heartbeat_delta.value()), timestamp_ns,
                    quality);
    add_measurement(context, "decode_reject_delta", "count",
                    static_cast<double>(decode_delta.value()), timestamp_ns,
                    quality);
    add_measurement(context, "queue_reject_delta", "count",
                    static_cast<double>(queue_delta.value()), timestamp_ns,
                    quality);
    add_measurement(context, "input_queue_drop_delta", "count",
                    static_cast<double>(drop_delta.value()), timestamp_ns,
                    quality);

    const auto max_age_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            criteria.max_heartbeat_age)
            .count();
    copy_snapshot_diagnostics(context, observation.latest);
    if (heartbeat_delta.value() < criteria.min_heartbeat_delta) {
      add_diagnostic(context, observation.latest,
                     DiagnosticSource::Communication, DiagnosticSeverity::Error,
                     "HEARTBEAT_STALLED",
                     "heartbeat count did not increase enough");
    }
    if (observation.max_heartbeat_age_ns < 0 ||
        observation.max_heartbeat_age_ns > max_age_ns) {
      add_diagnostic(context, observation.latest,
                     DiagnosticSource::Communication, DiagnosticSeverity::Error,
                     "HEARTBEAT_STALE",
                     "heartbeat age exceeded freshness threshold");
    }
    if (!observation.latest.device.online || observation.offline_after_seen) {
      add_diagnostic(context, observation.latest, DiagnosticSource::Device,
                     DiagnosticSeverity::Error, "DEVICE_OFFLINE",
                     "device left the online state during the window");
    }
    if (decode_delta.value() > criteria.max_decode_reject_delta) {
      add_diagnostic(context, observation.latest,
                     DiagnosticSource::Communication, DiagnosticSeverity::Error,
                     "MALFORMED_FRAME",
                     "decode rejects increased during the observation window",
                     "delta=" + std::to_string(decode_delta.value()));
    }

    TestEvaluation evaluation{};
    evaluation.criteria.push_back(
        criterion("heartbeat progress",
                  heartbeat_delta.value() >= criteria.min_heartbeat_delta,
                  ">=" + std::to_string(criteria.min_heartbeat_delta),
                  std::to_string(heartbeat_delta.value())));
    evaluation.criteria.push_back(criterion(
        "heartbeat freshness",
        observation.max_heartbeat_age_ns >= 0 &&
            observation.max_heartbeat_age_ns <= max_age_ns,
        "0.." + std::to_string(criteria.max_heartbeat_age.count()) + " ms",
        observation.max_heartbeat_age_ns < 0
            ? "unavailable"
            : std::to_string(observation.max_heartbeat_age_ns / 1'000'000) +
                  " ms"));
    evaluation.criteria.push_back(criterion(
        "device remained online",
        observation.latest.device.online && !observation.offline_after_seen,
        "online for observation window",
        observation.latest.device.online && !observation.offline_after_seen
            ? "online"
            : "offline observed"));
    evaluation.criteria.push_back(
        criterion("decode rejects",
                  decode_delta.value() <= criteria.max_decode_reject_delta,
                  "<=" + std::to_string(criteria.max_decode_reject_delta),
                  std::to_string(decode_delta.value())));
    evaluation.criteria.push_back(criterion(
        "queue rejects", queue_delta.value() <= criteria.max_queue_reject_delta,
        "<=" + std::to_string(criteria.max_queue_reject_delta),
        std::to_string(queue_delta.value())));
    evaluation.criteria.push_back(
        criterion("input queue drops",
                  drop_delta.value() <= criteria.max_input_queue_drop_delta,
                  "<=" + std::to_string(criteria.max_input_queue_drop_delta),
                  std::to_string(drop_delta.value())));

    const bool runtime_healthy =
        observation.latest.runtime.started &&
        observation.latest.runtime.scheduler_running &&
        observation.latest.runtime.fault == RuntimeFaultCode::None;
    evaluation.criteria.push_back(
        criterion("runtime remained healthy", runtime_healthy,
                  "started, scheduler running, fault NONE",
                  std::string{to_string(observation.latest.runtime.fault)}));

    const bool communication_healthy =
        !observation.latest.communication.input_queue_overflow_latched &&
        !observation.latest.device.comm_loss_latched &&
        !observation.latest.device.overflow_fault_latched &&
        observation.latest.device.device_fault_code == 0 &&
        observation.latest.communication.stop_reason ==
            CommunicationStopReason::None;
    evaluation.criteria.push_back(criterion(
        "communication fault flags", communication_healthy,
        "no latched fault and I/O stop reason NONE",
        communication_healthy ? "clear" : "fault or I/O stop observed"));

    evaluation.passed =
        std::all_of(evaluation.criteria.begin(), evaluation.criteria.end(),
                    [](const CriterionResult &item) { return item.passed; });
    evaluation.summary = evaluation.passed
                             ? "CAN communication health criteria passed"
                             : "CAN communication health criteria failed";
    if (!evaluation.passed) {
      for (const auto &item : evaluation.criteria) {
        if (item.passed) {
          continue;
        }
        add_diagnostic(context, observation.latest, DiagnosticSource::Test,
                       DiagnosticSeverity::Error, "CRITERION_FAILED",
                       item.name + " expected " + item.expected + " actual " +
                           item.actual,
                       "criterion=" + item.name);
      }
    }
    return evaluation;
  };

  // 本测试没有独占资源；cleanup 仍显式存在，以保持 TestRunner 的统一生命周期。
  test_case.cleanup = [](TestRunContext &) { return Result<void>::success(); };

  return runner.run(std::move(run_id), test_case);
}

} // namespace rcr::workbench
