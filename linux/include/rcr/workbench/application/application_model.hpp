#pragma once

// Workbench 应用层数据模型：只依赖 C++ 标准库，不泄漏 Runtime、SocketCAN 或 Qt
// 类型。 未来 Qt model 可以按自己的刷新频率复制这些
// snapshot，但不能据此取得控制权威。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rcr::workbench {

enum class EvidenceClass : std::uint8_t {
  Unspecified = 0,
  Mock = 1,
  Vcan = 2,
  Physical = 3,
  // Remote Workbench Boundary Gate：localhost 应用边界，不是物理 PC–ARM。
  Loopback = 4,
};

[[nodiscard]] constexpr std::string_view
to_string(EvidenceClass evidence) noexcept {
  switch (evidence) {
  case EvidenceClass::Unspecified:
    return "UNSPECIFIED";
  case EvidenceClass::Mock:
    return "MOCK";
  case EvidenceClass::Vcan:
    return "VCAN";
  case EvidenceClass::Physical:
    return "PHYSICAL";
  case EvidenceClass::Loopback:
    return "LOOPBACK";
  }
  return "UNKNOWN";
}

enum class DiagnosticSource : std::uint8_t {
  Runtime = 0,
  Communication = 1,
  Device = 2,
  Workbench = 3,
  Test = 4,
};

[[nodiscard]] constexpr std::string_view
to_string(DiagnosticSource source) noexcept {
  switch (source) {
  case DiagnosticSource::Runtime:
    return "RUNTIME";
  case DiagnosticSource::Communication:
    return "COMMUNICATION";
  case DiagnosticSource::Device:
    return "DEVICE";
  case DiagnosticSource::Workbench:
    return "WORKBENCH";
  case DiagnosticSource::Test:
    return "TEST";
  }
  return "UNKNOWN";
}

enum class DiagnosticSeverity : std::uint8_t {
  Info = 0,
  Warning = 1,
  Error = 2,
};

[[nodiscard]] constexpr std::string_view
to_string(DiagnosticSeverity severity) noexcept {
  switch (severity) {
  case DiagnosticSeverity::Info:
    return "INFO";
  case DiagnosticSeverity::Warning:
    return "WARNING";
  case DiagnosticSeverity::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

enum class CommandStatus : std::uint8_t {
  Accepted = 0,
  InvalidArgument = 1,
  NotOpen = 2,
  Busy = 3,
  Rejected = 4,
  Timeout = 5,
  IoError = 6,
  Unsupported = 7,
};

[[nodiscard]] constexpr std::string_view
to_string(CommandStatus status) noexcept {
  switch (status) {
  case CommandStatus::Accepted:
    return "ACCEPTED";
  case CommandStatus::InvalidArgument:
    return "INVALID_ARGUMENT";
  case CommandStatus::NotOpen:
    return "NOT_OPEN";
  case CommandStatus::Busy:
    return "BUSY";
  case CommandStatus::Rejected:
    return "REJECTED";
  case CommandStatus::Timeout:
    return "TIMEOUT";
  case CommandStatus::IoError:
    return "IO_ERROR";
  case CommandStatus::Unsupported:
    return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

enum class RuntimeModeCode : std::uint8_t {
  Disabled = 0,
  Idle,
  Active,
  Hold,
  Fault,
  EStop,
  Unknown,
};

[[nodiscard]] constexpr std::string_view
to_string(RuntimeModeCode mode) noexcept {
  switch (mode) {
  case RuntimeModeCode::Disabled:
    return "DISABLED";
  case RuntimeModeCode::Idle:
    return "IDLE";
  case RuntimeModeCode::Active:
    return "ACTIVE";
  case RuntimeModeCode::Hold:
    return "HOLD";
  case RuntimeModeCode::Fault:
    return "FAULT";
  case RuntimeModeCode::EStop:
    return "ESTOP";
  case RuntimeModeCode::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class RuntimeFaultCode : std::uint8_t {
  None = 0,
  Watchdog,
  InputFault,
  CommLoss,
  NodeFault,
  ProtocolReject,
  InterlockLost,
  Internal,
  AckTimeout,
  Unknown,
};

[[nodiscard]] constexpr std::string_view
to_string(RuntimeFaultCode fault) noexcept {
  switch (fault) {
  case RuntimeFaultCode::None:
    return "NONE";
  case RuntimeFaultCode::Watchdog:
    return "WATCHDOG";
  case RuntimeFaultCode::InputFault:
    return "INPUT_FAULT";
  case RuntimeFaultCode::CommLoss:
    return "COMM_LOSS";
  case RuntimeFaultCode::NodeFault:
    return "NODE_FAULT";
  case RuntimeFaultCode::ProtocolReject:
    return "PROTOCOL_REJECT";
  case RuntimeFaultCode::InterlockLost:
    return "INTERLOCK_LOST";
  case RuntimeFaultCode::Internal:
    return "INTERNAL";
  case RuntimeFaultCode::AckTimeout:
    return "ACK_TIMEOUT";
  case RuntimeFaultCode::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class RuntimeExitStatus : std::uint8_t {
  Ok = 0,
  ConfigError,
  InterfaceError,
  PermissionError,
  WorkerFailure,
  Unknown,
};

[[nodiscard]] constexpr std::string_view
to_string(RuntimeExitStatus status) noexcept {
  switch (status) {
  case RuntimeExitStatus::Ok:
    return "OK";
  case RuntimeExitStatus::ConfigError:
    return "CONFIG_ERROR";
  case RuntimeExitStatus::InterfaceError:
    return "INTERFACE_ERROR";
  case RuntimeExitStatus::PermissionError:
    return "PERMISSION_ERROR";
  case RuntimeExitStatus::WorkerFailure:
    return "WORKER_FAILURE";
  case RuntimeExitStatus::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class OutputApplyResult : std::uint8_t {
  Applied = 0,
  StaleSequence,
  SessionMismatch,
  Expired,
  InvalidMask,
  NotReady,
  Unknown,
};

[[nodiscard]] constexpr std::string_view
to_string(OutputApplyResult result) noexcept {
  switch (result) {
  case OutputApplyResult::Applied:
    return "APPLIED";
  case OutputApplyResult::StaleSequence:
    return "STALE_SEQUENCE";
  case OutputApplyResult::SessionMismatch:
    return "SESSION_MISMATCH";
  case OutputApplyResult::Expired:
    return "EXPIRED";
  case OutputApplyResult::InvalidMask:
    return "INVALID_MASK";
  case OutputApplyResult::NotReady:
    return "NOT_READY";
  case OutputApplyResult::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class CommunicationStopReason : std::uint8_t {
  None = 0,
  EventFd,
  Signal,
  IoError,
  SendFailure,
  Unknown,
};

[[nodiscard]] constexpr std::string_view
to_string(CommunicationStopReason reason) noexcept {
  switch (reason) {
  case CommunicationStopReason::None:
    return "NONE";
  case CommunicationStopReason::EventFd:
    return "EVENTFD";
  case CommunicationStopReason::Signal:
    return "SIGNAL";
  case CommunicationStopReason::IoError:
    return "IO_ERROR";
  case CommunicationStopReason::SendFailure:
    return "SEND_FAILURE";
  case CommunicationStopReason::Unknown:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

struct DiagnosticEvent {
  // snapshot 路径里这是观察时刻，不冒充原始故障发生时刻；写入 TestResult 后
  // 仍保持该语义，只多了可选 run_id/context 便于结果复核。
  std::int64_t observed_monotonic_ns{-1};
  DiagnosticSource source{DiagnosticSource::Workbench};
  DiagnosticSeverity severity{DiagnosticSeverity::Info};
  std::string code{};
  std::string message{};
  std::string device_id{};
  std::string run_id{};
  // 扁平 k=v;k=v，避免为诊断引入通用 map/JSON 对象模型。
  std::string context{};
};

struct RuntimeStateView {
  bool started{false};
  bool stopping{false};
  bool scheduler_running{false};
  bool interlock_ready{false};
  RuntimeModeCode mode{RuntimeModeCode::Disabled};
  RuntimeFaultCode fault{RuntimeFaultCode::None};
  RuntimeExitStatus exit_status{RuntimeExitStatus::Ok};
};

struct TimingSnapshot {
  std::int64_t target_period_ns{0};
  std::uint64_t cycles{0};
  std::uint64_t deadline_misses{0};
  std::int64_t min_lateness_ns{0};
  std::int64_t mean_lateness_ns{0};
  std::int64_t max_lateness_ns{0};
  bool fifo_enabled{false};
  int fifo_error{0};
  bool affinity_enabled{false};
  int affinity_error{0};
  int worker_error{0};
};

struct CommunicationView {
  std::string backend{};
  std::string interface_name{};
  EvidenceClass evidence{EvidenceClass::Unspecified};
  std::uint64_t frames_received{0};
  std::uint64_t frames_sent{0};
  std::uint64_t decode_rejects{0};
  std::uint64_t queue_rejects{0};
  std::uint64_t send_would_block_retries{0};
  std::uint64_t wakeups{0};
  std::size_t input_queue_capacity{0};
  std::size_t input_queue_size{0};
  std::uint64_t input_queue_drop_count{0};
  bool input_queue_overflow_latched{false};
  CommunicationStopReason stop_reason{CommunicationStopReason::None};
  int last_errno{0};
};

struct DeviceView {
  std::string device_id{};
  std::uint8_t node_id{0};
  bool ever_seen{false};
  bool online{false};
  bool restart_latched{false};
  bool overflow_fault_latched{false};
  bool comm_loss_latched{false};
  std::uint16_t boot_id{0};
  std::uint16_t session_id{0};
  std::uint16_t last_heartbeat_sequence{0};
  std::uint16_t device_fault_code{0};
  // CAN V1 NodeStatus.input_bits；bit0=POSITION_REACHED。CellReady 由应用层合成。
  std::uint16_t input_bits{0};
  // 最近一次 OutputStatus.output_mirror；只读，不参与 CellReady / Modbus DO0。
  std::uint8_t last_output_mirror{0};
  // -1 表示尚未观察到 heartbeat 或无法读取单调时钟。
  std::int64_t heartbeat_age_ns{-1};
  std::uint64_t heartbeats{0};
  std::uint64_t status_updates{0};
  std::uint64_t protocol_rejects{0};
};

struct OutputFeedbackView {
  bool ack_pending{false};
  std::uint16_t last_sent_session{0};
  std::uint16_t last_sent_sequence{0};
  std::int64_t last_sent_time_ns{0};
  std::uint16_t last_ack_session{0};
  std::uint16_t last_ack_sequence{0};
  OutputApplyResult last_ack_result{OutputApplyResult::Applied};
  std::int64_t last_ack_time_ns{0};
  std::uint64_t ack_timeout_count{0};
  std::uint64_t unexpected_ack_count{0};
};

struct RuntimeTelemetrySnapshot {
  std::int64_t observed_monotonic_ns{-1};
  RuntimeStateView runtime{};
  TimingSnapshot timing{};
  CommunicationView communication{};
  DeviceView device{};
  OutputFeedbackView output{};
  // 观测：CAN input_bits bit0。决策：Workbench CellReady，不是 CAN 字段。
  bool position_reached{false};
  bool cell_ready{false};
  std::vector<DiagnosticEvent> diagnostics{};
};

struct DigitalOutputRequest {
  std::uint64_t session_id{0};
  std::uint64_t sequence{0};
  std::uint32_t valid_for_ms{0};
  std::uint32_t mask{0};
  std::uint32_t values{0};
};

struct CommandReply {
  CommandStatus status{CommandStatus::Rejected};
  RuntimeModeCode from_state{RuntimeModeCode::Unknown};
  RuntimeModeCode to_state{RuntimeModeCode::Unknown};
  std::string message{};

  [[nodiscard]] bool accepted() const noexcept {
    return status == CommandStatus::Accepted;
  }
};

} // namespace rcr::workbench
