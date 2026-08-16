#pragma once

// 工程站 ↔ Orange Pi Cell 应用的有界 TCP。Magic 与 Modbus agent / Remote LOOPBACK
// 都不同：这不是 CAN 隧道，也不是 RTU 主站。

#include "rcr/result.hpp"
#include "rcr/workbench/application/application_model.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rcr::workbench {

inline constexpr std::uint32_t kCellAppMagic = 0x314C4543u; // 'CEL1'
inline constexpr std::uint8_t kCellAppVersion = 1;
inline constexpr std::size_t kCellAppHeaderSize = 12;
inline constexpr std::size_t kCellAppCrcSize = 2;
inline constexpr std::size_t kCellAppMaxPayload = 256;
inline constexpr std::uint16_t kCellAppDefaultPort = 5750;
// 有效字段到 DO0 status 为止 77 字节，其余填充，冻结为 80。
inline constexpr std::size_t kCellAppStatusWireSize = 80;
// DigitalOutputRequest 全宽：u64 session/sequence + u32 valid_for_ms/mask/values。
// CAN V1 输出仍只接受 u16 session/sequence 与 u8 mask/values；Adapter 拒绝超范围值，
// CEL1 不在线上静默截断。
inline constexpr std::size_t kCellAppOutputPayloadSize = 28;

enum class CellAppMessage : std::uint8_t {
  GetStatus = 1,
  GetStatusAck = 2,
  Activate = 3,
  ActivateAck = 4,
  SubmitOutput = 5,
  SubmitOutputAck = 6,
  Error = 7,
};

struct CellAppFrame {
  CellAppMessage type{CellAppMessage::Error};
  std::uint16_t sequence{0};
  std::vector<std::uint8_t> payload{};
};

// 工程站 Overview / Runtime 所需的稳定子集。闭环决策在边缘已经算完。
struct CellAppStatus {
  std::int64_t observed_monotonic_ns{-1};
  RuntimeModeCode mode{RuntimeModeCode::Unknown};
  RuntimeFaultCode fault{RuntimeFaultCode::None};
  bool started{false};
  bool interlock_ready{false};
  bool online{false};
  bool position_reached{false};
  bool cell_ready{false};
  std::uint8_t node_id{0};
  std::uint16_t boot_id{0};
  std::uint16_t session_id{0};
  std::uint16_t last_heartbeat_sequence{0};
  std::int64_t heartbeat_age_ns{-1};
  std::uint16_t input_bits{0};
  std::uint16_t device_fault_code{0};
  std::uint64_t frames_received{0};
  std::uint64_t frames_sent{0};
  std::uint64_t decode_rejects{0};
  std::uint64_t input_queue_drop_count{0};
  std::uint16_t last_ack_session{0};
  std::uint16_t last_ack_sequence{0};
  OutputApplyResult last_ack_result{OutputApplyResult::Unknown};
  bool ack_pending{false};
  EvidenceClass evidence{EvidenceClass::Unspecified};
  // 占用 80 字节尾部填充：边缘 Cell I/O，不是第二套 CAN 字段。
  bool modbus_online{false};
  bool cell_ready_do0_requested{false};
  bool cell_ready_do0_confirmed{false};
  std::uint8_t cell_ready_do0_status{0};
};

[[nodiscard]] bool encode_cell_app_frame(const CellAppFrame &frame,
                                         std::vector<std::uint8_t> &out);

[[nodiscard]] Result<CellAppFrame>
try_decode_cell_app_frame(std::span<const std::uint8_t> bytes,
                          std::size_t &consumed);

[[nodiscard]] bool encode_cell_app_status(const CellAppStatus &status,
                                          std::vector<std::uint8_t> &out);

[[nodiscard]] Result<CellAppStatus>
decode_cell_app_status(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t>
encode_cell_output_payload(const DigitalOutputRequest &request);

[[nodiscard]] Result<DigitalOutputRequest>
decode_cell_output_payload(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t>
encode_cell_command_reply(const CommandReply &reply);

[[nodiscard]] Result<CommandReply>
decode_cell_command_reply(std::span<const std::uint8_t> payload);

[[nodiscard]] CellAppStatus
project_cell_app_status(const RuntimeTelemetrySnapshot &snapshot) noexcept;

[[nodiscard]] RuntimeTelemetrySnapshot
cell_status_to_snapshot(const CellAppStatus &status);

} // namespace rcr::workbench
