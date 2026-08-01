#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// 线级权威合同：protocol/can_v1/README.md（protocol_version = 1）。

#include "rcr/result.hpp"
#include "rcr/types.hpp"

#include <cstdint>

namespace rcr::can_v1 {

inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::uint8_t kMaxNodeId = 31;
inline constexpr std::uint8_t kMinValidity10ms = 1;
inline constexpr std::uint8_t kMaxValidity10ms = 250;
inline constexpr std::int64_t kValidityUnitNs = 10'000'000LL;  // 10 ms

enum class Function : std::uint8_t {
  Heartbeat = 0x01,
  Status = 0x02,
  OutputCommand = 0x03,
  OutputStatus = 0x04,
};

/// OutputStatus.result 低 4 bit；6..15 保留，编解码均拒绝。
enum class OutputResult : std::uint8_t {
  Applied = 0,
  StaleSequence = 1,
  SessionMismatch = 2,
  Expired = 3,
  InvalidMask = 4,
  NotReady = 5,
};

/**
 * 线级 DTO：与 rcr::OutputCommand 等进程内类型分离。
 * 字段宽度、单位与合同一致；不含 padding 依赖，不得整结构 memcpy 上总线。
 */
struct WireHeartbeat {
  std::uint8_t node_id{0};
  std::uint16_t boot_id{0};
  std::uint16_t session_id{0};
  std::uint16_t hb_seq{0};
};

struct WireNodeStatus {
  std::uint8_t node_id{0};
  bool interlock_ready{false};
  std::uint16_t session_id{0};
  std::uint16_t input_bits{0};
  std::uint16_t fault_code{0};
};

struct WireOutputCommand {
  std::uint8_t node_id{0};
  std::uint8_t mask{0};
  std::uint16_t session_id{0};
  std::uint16_t sequence{0};
  std::uint8_t values{0};
  std::uint8_t validity_10ms{0};
};

struct WireOutputStatus {
  std::uint8_t node_id{0};
  OutputResult result{OutputResult::Applied};
  std::uint16_t session_id{0};
  std::uint16_t sequence{0};
  std::uint8_t output_mirror{0};
};

[[nodiscard]] constexpr std::uint32_t make_can_id(Function function,
                                                  std::uint8_t node_id) noexcept {
  return (static_cast<std::uint32_t>(function) << 5) |
         (static_cast<std::uint32_t>(node_id) & 0x1Fu);
}

[[nodiscard]] constexpr std::uint8_t node_id_from_can_id(std::uint32_t raw11) noexcept {
  return static_cast<std::uint8_t>(raw11 & 0x1Fu);
}

[[nodiscard]] constexpr std::uint8_t function_from_can_id(std::uint32_t raw11) noexcept {
  return static_cast<std::uint8_t>((raw11 >> 5) & 0x3Fu);
}

/// u16 序号算术：a 是否比 b 更新（合同 §4.5）。
[[nodiscard]] constexpr bool seq_newer(std::uint16_t a, std::uint16_t b) noexcept {
  return a != b && static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b)) > 0;
}

/**
 * 接收端：相对有效期 → 本地 CLOCK_MONOTONIC deadline（纳秒）。
 * 调用前 validity 必须已通过 decode 范围校验。
 */
[[nodiscard]] constexpr std::int64_t deadline_from_validity_10ms(
    std::int64_t receive_monotonic_ns, std::uint8_t validity_10ms) noexcept {
  return receive_monotonic_ns +
         static_cast<std::int64_t>(validity_10ms) * kValidityUnitNs;
}

/**
 * 发送端：绝对 deadline → validity_10ms。
 * 剩余有效期须落在 [10ms, 2500ms]，否则 encode 失败，避免线上非法值。
 */
[[nodiscard]] Result<std::uint8_t> validity_10ms_from_deadline(std::int64_t now_ns,
                                                              std::int64_t deadline_ns);

[[nodiscard]] Result<CanFrame> encode_heartbeat(const WireHeartbeat& msg);
[[nodiscard]] Result<CanFrame> encode_node_status(const WireNodeStatus& msg);
[[nodiscard]] Result<CanFrame> encode_output_command(const WireOutputCommand& msg);
[[nodiscard]] Result<CanFrame> encode_output_status(const WireOutputStatus& msg);

[[nodiscard]] Result<WireHeartbeat> decode_heartbeat(const CanFrame& frame);
[[nodiscard]] Result<WireNodeStatus> decode_node_status(const CanFrame& frame);
[[nodiscard]] Result<WireOutputCommand> decode_output_command(const CanFrame& frame);
[[nodiscard]] Result<WireOutputStatus> decode_output_status(const CanFrame& frame);

/**
 * 按 CAN ID 分发解码。未知 function / 帧层非法 → Rejected。
 * 成功时 kind 与对应字段有效；其余字段未定义。
 */
enum class MessageKind : std::uint8_t {
  Heartbeat = 0,
  Status = 1,
  OutputCommand = 2,
  OutputStatus = 3,
};

struct DecodedMessage {
  MessageKind kind{MessageKind::Heartbeat};
  WireHeartbeat heartbeat{};
  WireNodeStatus status{};
  WireOutputCommand command{};
  WireOutputStatus output_status{};
};

[[nodiscard]] Result<DecodedMessage> decode(const CanFrame& frame);

}  // namespace rcr::can_v1
