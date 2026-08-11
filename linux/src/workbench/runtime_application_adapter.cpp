#include "rcr/workbench/runtime_application_adapter.hpp"

#include "rcr/can_v1.hpp"
#include "rcr/runtime_daemon.hpp"
#include "rcr/time.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace rcr::workbench {
namespace {

CommandStatus map_error(Errc code) noexcept {
  switch (code) {
  case Errc::Ok:
    return CommandStatus::Accepted;
  case Errc::InvalidArgument:
    return CommandStatus::InvalidArgument;
  case Errc::NotOpen:
    return CommandStatus::NotOpen;
  case Errc::IoError:
  case Errc::WouldBlock:
    return CommandStatus::IoError;
  case Errc::Timeout:
    return CommandStatus::Timeout;
  case Errc::Busy:
    return CommandStatus::Busy;
  case Errc::Rejected:
    return CommandStatus::Rejected;
  case Errc::Unsupported:
    return CommandStatus::Unsupported;
  }
  return CommandStatus::Rejected;
}

RuntimeModeCode map_runtime_mode(rcr::RuntimeMode mode) noexcept {
  switch (mode) {
  case rcr::RuntimeMode::Disabled:
    return RuntimeModeCode::Disabled;
  case rcr::RuntimeMode::Idle:
    return RuntimeModeCode::Idle;
  case rcr::RuntimeMode::Active:
    return RuntimeModeCode::Active;
  case rcr::RuntimeMode::Hold:
    return RuntimeModeCode::Hold;
  case rcr::RuntimeMode::Fault:
    return RuntimeModeCode::Fault;
  case rcr::RuntimeMode::EStop:
    return RuntimeModeCode::EStop;
  }
  return RuntimeModeCode::Unknown;
}

RuntimeFaultCode map_runtime_fault(rcr::FaultCode fault) noexcept {
  switch (fault) {
  case rcr::FaultCode::None:
    return RuntimeFaultCode::None;
  case rcr::FaultCode::Watchdog:
    return RuntimeFaultCode::Watchdog;
  case rcr::FaultCode::InputFault:
    return RuntimeFaultCode::InputFault;
  case rcr::FaultCode::CommLoss:
    return RuntimeFaultCode::CommLoss;
  case rcr::FaultCode::NodeFault:
    return RuntimeFaultCode::NodeFault;
  case rcr::FaultCode::ProtocolReject:
    return RuntimeFaultCode::ProtocolReject;
  case rcr::FaultCode::InterlockLost:
    return RuntimeFaultCode::InterlockLost;
  case rcr::FaultCode::Internal:
    return RuntimeFaultCode::Internal;
  case rcr::FaultCode::AckTimeout:
    return RuntimeFaultCode::AckTimeout;
  }
  return RuntimeFaultCode::Unknown;
}

RuntimeExitStatus map_exit_status(DaemonExitCode status) noexcept {
  switch (status) {
  case DaemonExitCode::Ok:
    return RuntimeExitStatus::Ok;
  case DaemonExitCode::ConfigError:
    return RuntimeExitStatus::ConfigError;
  case DaemonExitCode::InterfaceError:
    return RuntimeExitStatus::InterfaceError;
  case DaemonExitCode::PermissionError:
    return RuntimeExitStatus::PermissionError;
  case DaemonExitCode::WorkerFailure:
    return RuntimeExitStatus::WorkerFailure;
  }
  return RuntimeExitStatus::Unknown;
}

OutputApplyResult map_output_result(can_v1::OutputResult result) noexcept {
  switch (result) {
  case can_v1::OutputResult::Applied:
    return OutputApplyResult::Applied;
  case can_v1::OutputResult::StaleSequence:
    return OutputApplyResult::StaleSequence;
  case can_v1::OutputResult::SessionMismatch:
    return OutputApplyResult::SessionMismatch;
  case can_v1::OutputResult::Expired:
    return OutputApplyResult::Expired;
  case can_v1::OutputResult::InvalidMask:
    return OutputApplyResult::InvalidMask;
  case can_v1::OutputResult::NotReady:
    return OutputApplyResult::NotReady;
  }
  return OutputApplyResult::Unknown;
}

CommunicationStopReason map_stop_reason(IoStopReason reason) noexcept {
  switch (reason) {
  case IoStopReason::None:
    return CommunicationStopReason::None;
  case IoStopReason::EventFd:
    return CommunicationStopReason::EventFd;
  case IoStopReason::Signal:
    return CommunicationStopReason::Signal;
  case IoStopReason::IoError:
    return CommunicationStopReason::IoError;
  case IoStopReason::SendFailure:
    return CommunicationStopReason::SendFailure;
  }
  return CommunicationStopReason::Unknown;
}

CommandReply transition_reply(const TransitionResult &transition) {
  return CommandReply{transition.accepted ? CommandStatus::Accepted
                                          : CommandStatus::Rejected,
                      map_runtime_mode(transition.from),
                      map_runtime_mode(transition.to), transition.reason};
}

void add_diagnostic(RuntimeTelemetrySnapshot &out, DiagnosticSource source,
                    DiagnosticSeverity severity, std::string code,
                    std::string message) {
  out.diagnostics.push_back(DiagnosticEvent{
      out.observed_monotonic_ns, source, severity, std::move(code),
      std::move(message), out.device.device_id});
}

void project_diagnostics(const DaemonSnapshot &raw,
                         RuntimeTelemetrySnapshot &out) {
  if (!raw.started) {
    add_diagnostic(out, DiagnosticSource::Runtime, DiagnosticSeverity::Info,
                   "RUNTIME_NOT_STARTED", "Runtime daemon is not started");
  }
  if (raw.runtime.fault != FaultCode::None) {
    add_diagnostic(out, DiagnosticSource::Runtime, DiagnosticSeverity::Error,
                   "RUNTIME_FAULT",
                   std::string(rcr::to_string(raw.runtime.fault)));
  }
  if (raw.runtime.scheduler.worker_error != 0) {
    add_diagnostic(out, DiagnosticSource::Runtime, DiagnosticSeverity::Error,
                   "SCHEDULER_WORKER_ERROR",
                   "scheduler worker errno=" +
                       std::to_string(raw.runtime.scheduler.worker_error));
  }
  if (raw.input_queue_overflow_latched) {
    add_diagnostic(out, DiagnosticSource::Communication,
                   DiagnosticSeverity::Error, "INPUT_QUEUE_OVERFLOW",
                   "Runtime input queue overflow is latched");
  }
  if (raw.node.ever_seen && !raw.node.online) {
    add_diagnostic(out, DiagnosticSource::Communication,
                   DiagnosticSeverity::Error, "DEVICE_OFFLINE",
                   "Previously observed device is offline");
  }
  if (raw.node.comm_loss_latched) {
    add_diagnostic(out, DiagnosticSource::Communication,
                   DiagnosticSeverity::Error, "COMM_LOSS_LATCHED",
                   "Communication loss is latched");
  }
  if (raw.io.decode_rejects != 0 || raw.node.protocol_rejects != 0) {
    add_diagnostic(out, DiagnosticSource::Communication,
                   DiagnosticSeverity::Warning, "PROTOCOL_REJECTS",
                   "One or more received frames were rejected");
  }
  if (raw.node.node_fault_code != 0) {
    add_diagnostic(out, DiagnosticSource::Device, DiagnosticSeverity::Error,
                   "DEVICE_FAULT",
                   "device fault code=" +
                       std::to_string(raw.node.node_fault_code));
  }
  if (raw.io.stop_reason == IoStopReason::IoError ||
      raw.io.stop_reason == IoStopReason::SendFailure) {
    add_diagnostic(out, DiagnosticSource::Communication,
                   DiagnosticSeverity::Error, "CAN_IO_STOPPED",
                   std::string(rcr::to_string(raw.io.stop_reason)) +
                       " errno=" + std::to_string(raw.io.last_errno));
  }
}

} // namespace

RuntimeApplicationAdapter::RuntimeApplicationAdapter(
    RuntimeDaemon &daemon, RuntimeApplicationAdapterConfig config)
    : daemon_(daemon), config_(std::move(config)) {}

RuntimeTelemetrySnapshot RuntimeApplicationAdapter::snapshot() const {
  const auto raw = daemon_.snapshot();
  const auto now = monotonic_now_ns();
  const auto &daemon_config = daemon_.config();

  RuntimeTelemetrySnapshot out{};
  if (now) {
    out.observed_monotonic_ns = now.value();
  }

  out.runtime.started = raw.started;
  out.runtime.stopping = raw.stopping;
  out.runtime.scheduler_running = raw.runtime.running;
  out.runtime.interlock_ready = raw.runtime.interlock_ready;
  out.runtime.mode = map_runtime_mode(raw.runtime.mode);
  out.runtime.fault = map_runtime_fault(raw.runtime.fault);
  out.runtime.exit_status = map_exit_status(raw.exit_code);

  out.timing.target_period_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(daemon_config.period)
          .count();
  out.timing.cycles = raw.runtime.scheduler.cycles;
  out.timing.deadline_misses = raw.runtime.scheduler.deadline_misses;
  out.timing.min_lateness_ns = raw.runtime.scheduler.min_lateness_ns;
  out.timing.mean_lateness_ns = raw.runtime.scheduler.mean_lateness_ns;
  out.timing.max_lateness_ns = raw.runtime.scheduler.max_lateness_ns;
  out.timing.fifo_enabled = raw.runtime.scheduler.fifo_enabled;
  out.timing.fifo_error = raw.runtime.scheduler.fifo_error;
  out.timing.affinity_enabled = raw.runtime.scheduler.affinity_enabled;
  out.timing.affinity_error = raw.runtime.scheduler.affinity_error;
  out.timing.worker_error = raw.runtime.scheduler.worker_error;

  out.communication.backend = config_.backend_label;
  out.communication.interface_name = daemon_config.can_if;
  out.communication.evidence = config_.evidence;
  out.communication.frames_received = raw.io.frames_received;
  out.communication.frames_sent = raw.io.frames_sent;
  out.communication.decode_rejects = raw.io.decode_rejects;
  out.communication.queue_rejects = raw.io.queue_rejects;
  out.communication.send_would_block_retries = raw.io.send_would_block_retries;
  out.communication.wakeups = raw.io.wakeups;
  out.communication.input_queue_capacity = raw.input_queue_capacity;
  out.communication.input_queue_size = raw.input_queue_size;
  out.communication.input_queue_drop_count = raw.input_queue_drop_count;
  out.communication.input_queue_overflow_latched =
      raw.input_queue_overflow_latched;
  out.communication.stop_reason = map_stop_reason(raw.io.stop_reason);
  out.communication.last_errno = raw.io.last_errno;

  out.device.node_id = daemon_config.node_id;
  out.device.device_id = "CAN_NODE_" + std::to_string(daemon_config.node_id);
  out.device.ever_seen = raw.node.ever_seen;
  out.device.online = raw.node.online;
  out.device.restart_latched = raw.node.restart_latched;
  out.device.overflow_fault_latched = raw.node.overflow_fault_latched;
  out.device.comm_loss_latched = raw.node.comm_loss_latched;
  out.device.boot_id = raw.node.boot_id;
  out.device.session_id = raw.node.session_id;
  out.device.last_heartbeat_sequence = raw.node.last_hb_seq;
  out.device.device_fault_code = raw.node.node_fault_code;
  out.device.heartbeats = raw.node.heartbeats;
  out.device.status_updates = raw.node.status_updates;
  out.device.protocol_rejects = raw.node.protocol_rejects;
  if (now && raw.node.ever_seen && raw.node.last_heartbeat_ns > 0) {
    out.device.heartbeat_age_ns = now.value() >= raw.node.last_heartbeat_ns
                                      ? now.value() - raw.node.last_heartbeat_ns
                                      : 0;
  }

  out.output.ack_pending = raw.runtime.output_ack_pending;
  out.output.last_sent_session = raw.runtime.last_sent_session;
  out.output.last_sent_sequence = raw.runtime.last_sent_sequence;
  out.output.last_sent_time_ns = raw.runtime.last_sent_time_ns;
  out.output.last_ack_session = raw.runtime.last_ack_session;
  out.output.last_ack_sequence = raw.runtime.last_ack_sequence;
  out.output.last_ack_result = map_output_result(raw.runtime.last_ack_result);
  out.output.last_ack_time_ns = raw.runtime.last_ack_time_ns;
  out.output.ack_timeout_count = raw.runtime.ack_timeout_count;
  out.output.unexpected_ack_count = raw.runtime.unexpected_ack_count;

  if (!now) {
    add_diagnostic(out, DiagnosticSource::Workbench, DiagnosticSeverity::Error,
                   "MONOTONIC_CLOCK_ERROR", now.error().message());
  }
  project_diagnostics(raw, out);
  return out;
}

CommandReply RuntimeApplicationAdapter::activate() {
  return transition_reply(daemon_.activate());
}

CommandReply RuntimeApplicationAdapter::deactivate() {
  return transition_reply(daemon_.deactivate());
}

CommandReply RuntimeApplicationAdapter::clear_fault() {
  return transition_reply(daemon_.clear_fault());
}

CommandReply RuntimeApplicationAdapter::submit_digital_output(
    const DigitalOutputRequest &request) {
  constexpr std::uint64_t kMaxWireValue =
      std::numeric_limits<std::uint16_t>::max();
  constexpr std::uint32_t kMaxDigitalBits =
      std::numeric_limits<std::uint8_t>::max();
  constexpr std::uint32_t kMinValidityMs =
      static_cast<std::uint32_t>(can_v1::kMinValidity10ms) * 10U;
  constexpr std::uint32_t kMaxValidityMs =
      static_cast<std::uint32_t>(can_v1::kMaxValidity10ms) * 10U;

  const auto current = map_runtime_mode(daemon_.snapshot().runtime.mode);
  const auto invalid = [&current](std::string message) {
    return CommandReply{CommandStatus::InvalidArgument, current, current,
                        std::move(message)};
  };

  if (request.session_id == 0 || request.session_id > kMaxWireValue) {
    return invalid("session_id must be 1..65535");
  }
  if (request.sequence == 0 || request.sequence > kMaxWireValue) {
    return invalid("sequence must be 1..65535");
  }
  if (request.valid_for_ms < kMinValidityMs ||
      request.valid_for_ms > kMaxValidityMs) {
    return invalid("valid_for_ms must be 10..2500");
  }
  if (request.mask == 0 || request.mask > kMaxDigitalBits ||
      request.values > kMaxDigitalBits) {
    return invalid("mask must be nonzero u8 and values must fit u8");
  }

  const auto now = monotonic_now_ns();
  if (!now) {
    return CommandReply{CommandStatus::IoError, current, current,
                        now.error().message()};
  }
  const auto validity_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::milliseconds{request.valid_for_ms})
                               .count();
  if (now.value() > std::numeric_limits<std::int64_t>::max() - validity_ns) {
    return invalid("deadline overflows CLOCK_MONOTONIC nanoseconds");
  }

  const OutputCommand command{request.session_id, request.sequence,
                              now.value() + validity_ns, request.mask,
                              request.values};
  const auto published = daemon_.publish_output_command(command);
  if (!published) {
    return CommandReply{map_error(published.error().code()), current, current,
                        published.error().message()};
  }
  return CommandReply{CommandStatus::Accepted, current, current,
                      "digital output command accepted by Runtime"};
}

} // namespace rcr::workbench
