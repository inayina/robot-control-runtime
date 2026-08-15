#pragma once

// PC commissioning ↔ Orange Pi RTU 主站的有界 TCP 帧。Magic 与 Runtime Remote
// 控制面不同，避免把 HELLO/GET_STATUS 和现场 I/O 事务混成一条总线。

#include "rcr/result.hpp"
#include "rcr/workbench/profile/mock_modbus_io_profile.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rcr::workbench {

inline constexpr std::uint32_t kModbusAgentMagic = 0x4D524352u; // 'RCRM'
inline constexpr std::uint8_t kModbusAgentVersion = 1;
inline constexpr std::size_t kModbusAgentHeaderSize = 12;
inline constexpr std::size_t kModbusAgentCrcSize = 2;
inline constexpr std::size_t kModbusAgentMaxPayload = 256;
inline constexpr std::uint16_t kModbusAgentDefaultPort = 5740;

enum class ModbusAgentMessage : std::uint8_t {
  Probe = 1,
  ProbeAck = 2,
  ReadDi = 3,
  ReadDiAck = 4,
  WriteDo = 5,
  WriteDoAck = 6,
  Error = 7,
  AllOff = 8,
  AllOffAck = 9,
};

struct ModbusAgentFrame {
  ModbusAgentMessage type{ModbusAgentMessage::Error};
  std::uint16_t sequence{0};
  std::vector<std::uint8_t> payload{};
};

[[nodiscard]] bool encode_modbus_agent_frame(const ModbusAgentFrame &frame,
                                             std::vector<std::uint8_t> &out);

[[nodiscard]] Result<ModbusAgentFrame>
try_decode_modbus_agent_frame(std::span<const std::uint8_t> bytes,
                              std::size_t &consumed);

[[nodiscard]] std::vector<std::uint8_t>
encode_probe_ack_payload(const ModbusIoSnapshot &snapshot);

[[nodiscard]] Result<ModbusIoSnapshot>
decode_probe_ack_payload(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t>
encode_write_do_payload(std::uint8_t channel, bool active);

[[nodiscard]] Result<std::pair<std::uint8_t, bool>>
decode_write_do_payload(std::span<const std::uint8_t> payload);

} // namespace rcr::workbench
