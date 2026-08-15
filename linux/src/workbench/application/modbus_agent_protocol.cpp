#include "rcr/workbench/application/modbus_agent_protocol.hpp"

#include "rcr/workbench/services/modbus_rtu.hpp"

#include <algorithm>

namespace rcr::workbench {
namespace {

void append_u16_le(std::vector<std::uint8_t> &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_u32_le(std::vector<std::uint8_t> &out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t *p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t *p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

void append_lp_string(std::vector<std::uint8_t> &out, const std::string &value) {
  const auto n = static_cast<std::uint8_t>(
      std::min(value.size(), static_cast<std::size_t>(255)));
  out.push_back(n);
  out.insert(out.end(), value.begin(), value.begin() + n);
}

Result<std::string> read_lp_string(std::span<const std::uint8_t> in,
                                   std::size_t &offset) {
  if (offset >= in.size()) {
    return Error{Errc::InvalidArgument, "truncated agent string"};
  }
  const std::uint8_t n = in[offset++];
  if (offset + n > in.size()) {
    return Error{Errc::InvalidArgument, "truncated agent string body"};
  }
  std::string out(reinterpret_cast<const char *>(in.data() + offset), n);
  offset += n;
  return out;
}

} // namespace

bool encode_modbus_agent_frame(const ModbusAgentFrame &frame,
                               std::vector<std::uint8_t> &out) {
  if (frame.payload.size() > kModbusAgentMaxPayload) {
    return false;
  }
  out.clear();
  append_u32_le(out, kModbusAgentMagic);
  out.push_back(kModbusAgentVersion);
  out.push_back(static_cast<std::uint8_t>(frame.type));
  out.push_back(0);
  out.push_back(0);
  append_u16_le(out, frame.sequence);
  append_u16_le(out, static_cast<std::uint16_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  const auto crc = modbus_rtu_crc16(
      std::span<const std::uint8_t>(out.data(), out.size()));
  append_u16_le(out, crc);
  return true;
}

Result<ModbusAgentFrame>
try_decode_modbus_agent_frame(std::span<const std::uint8_t> bytes,
                              std::size_t &consumed) {
  consumed = 0;
  if (bytes.size() < kModbusAgentHeaderSize + kModbusAgentCrcSize) {
    return Error{Errc::WouldBlock, "need more agent bytes"};
  }
  if (read_u32_le(bytes.data()) != kModbusAgentMagic) {
    return Error{Errc::Rejected, "bad agent magic"};
  }
  if (bytes[4] != kModbusAgentVersion) {
    return Error{Errc::Unsupported, "unsupported agent version"};
  }
  const auto payload_len = read_u16_le(bytes.data() + 10);
  if (payload_len > kModbusAgentMaxPayload) {
    return Error{Errc::InvalidArgument, "agent payload too large"};
  }
  const std::size_t total =
      kModbusAgentHeaderSize + payload_len + kModbusAgentCrcSize;
  if (bytes.size() < total) {
    return Error{Errc::WouldBlock, "need more agent bytes"};
  }
  const auto crc_got = read_u16_le(bytes.data() + total - 2);
  const auto crc_calc = modbus_rtu_crc16(bytes.first(total - 2));
  if (crc_got != crc_calc) {
    return Error{Errc::Rejected, "agent CRC mismatch"};
  }
  ModbusAgentFrame frame;
  frame.type = static_cast<ModbusAgentMessage>(bytes[5]);
  frame.sequence = read_u16_le(bytes.data() + 8);
  frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kModbusAgentHeaderSize),
                       bytes.begin() + static_cast<std::ptrdiff_t>(kModbusAgentHeaderSize +
                                                                   payload_len));
  consumed = total;
  return frame;
}

std::vector<std::uint8_t>
encode_probe_ack_payload(const ModbusIoSnapshot &snapshot) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(snapshot.device_state));
  out.push_back(snapshot.slave_id);
  out.push_back(static_cast<std::uint8_t>(kModbusIoChannelCount));
  for (const auto &input : snapshot.digital_inputs) {
    out.push_back(input.active ? 1 : 0);
  }
  for (const auto &output : snapshot.digital_outputs) {
    out.push_back(output.requested ? 1 : 0);
    out.push_back(output.confirmed ? 1 : 0);
    out.push_back(static_cast<std::uint8_t>(output.last_status));
  }
  append_u32_le(out, snapshot.baud_rate);
  out.push_back(snapshot.parity == "None" ? static_cast<std::uint8_t>('N')
                                          : static_cast<std::uint8_t>(snapshot.parity.empty()
                                                                        ? 'N'
                                                                        : snapshot.parity[0]));
  append_u32_le(out, static_cast<std::uint32_t>(
                         snapshot.last_transaction.rtt_ns / 1'000'000));
  out.push_back(static_cast<std::uint8_t>(snapshot.last_command_status));
  out.push_back(snapshot.last_transaction.function);
  append_lp_string(out, snapshot.serial_port);
  append_lp_string(out, snapshot.sku);
  append_lp_string(out, snapshot.last_error);
  append_lp_string(out, snapshot.last_transaction.tx_hex);
  append_lp_string(out, snapshot.last_transaction.rx_hex);
  append_lp_string(out, snapshot.last_transaction.result);
  return out;
}

Result<ModbusIoSnapshot>
decode_probe_ack_payload(std::span<const std::uint8_t> payload) {
  constexpr std::size_t kFixed =
      3 + kModbusIoChannelCount + (kModbusIoChannelCount * 3) + 4 + 1 + 4 + 1 + 1;
  if (payload.size() < kFixed) {
    return Error{Errc::InvalidArgument, "probe ack too short"};
  }
  ModbusIoSnapshot snapshot;
  snapshot.backend = "PHYSICAL";
  snapshot.evidence = EvidenceClass::Physical;
  snapshot.no_physical_rs485 = false;
  snapshot.transport = "Modbus RTU";
  snapshot.device_state = static_cast<ModbusDeviceState>(payload[0]);
  snapshot.slave_id = payload[1];
  const std::uint8_t count = payload[2];
  if (count != kModbusIoChannelCount) {
    return Error{Errc::InvalidArgument, "unexpected DI count"};
  }
  std::size_t offset = 3;
  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    snapshot.digital_inputs[channel].channel =
        static_cast<std::uint8_t>(channel);
    snapshot.digital_inputs[channel].active = payload[offset++] != 0;
  }
  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    snapshot.digital_outputs[channel].channel =
        static_cast<std::uint8_t>(channel);
    snapshot.digital_outputs[channel].requested = payload[offset++] != 0;
    snapshot.digital_outputs[channel].confirmed = payload[offset++] != 0;
    snapshot.digital_outputs[channel].last_status =
        static_cast<ModbusIoCommandStatus>(payload[offset++]);
  }
  snapshot.baud_rate = read_u32_le(payload.data() + offset);
  snapshot.baud_rate_placeholder = snapshot.baud_rate;
  offset += 4;
  const char parity = static_cast<char>(payload[offset++]);
  snapshot.parity = parity == 'N' ? "None" : std::string(1, parity);
  snapshot.parity_placeholder = snapshot.parity;
  const auto rtt_ms = read_u32_le(payload.data() + offset);
  offset += 4;
  snapshot.last_transaction.rtt_ns =
      static_cast<std::int64_t>(rtt_ms) * 1'000'000;
  snapshot.last_command_status =
      static_cast<ModbusIoCommandStatus>(payload[offset++]);
  snapshot.last_transaction.function = payload[offset++];
  auto serial = read_lp_string(payload, offset);
  if (!serial) {
    return serial.error();
  }
  snapshot.serial_port = std::move(serial.value());
  auto sku = read_lp_string(payload, offset);
  if (!sku) {
    return sku.error();
  }
  snapshot.sku = std::move(sku.value());
  auto error = read_lp_string(payload, offset);
  if (!error) {
    return error.error();
  }
  snapshot.last_error = std::move(error.value());
  auto tx = read_lp_string(payload, offset);
  if (!tx) {
    return tx.error();
  }
  snapshot.last_transaction.tx_hex = std::move(tx.value());
  auto rx = read_lp_string(payload, offset);
  if (!rx) {
    return rx.error();
  }
  snapshot.last_transaction.rx_hex = std::move(rx.value());
  auto result = read_lp_string(payload, offset);
  if (!result) {
    return result.error();
  }
  snapshot.last_transaction.result = std::move(result.value());
  snapshot.last_transaction.slave_id = snapshot.slave_id;
  snapshot.scan_state = snapshot.device_state == ModbusDeviceState::Online
                            ? ModbusScanState::Complete
                            : ModbusScanState::Error;
  return snapshot;
}

std::vector<std::uint8_t> encode_write_do_payload(std::uint8_t channel,
                                                 bool active) {
  return {channel, static_cast<std::uint8_t>(active ? 1 : 0)};
}

Result<std::pair<std::uint8_t, bool>>
decode_write_do_payload(std::span<const std::uint8_t> payload) {
  if (payload.size() < 2) {
    return Error{Errc::InvalidArgument, "write-do payload too short"};
  }
  return std::pair<std::uint8_t, bool>{payload[0], payload[1] != 0};
}

} // namespace rcr::workbench
