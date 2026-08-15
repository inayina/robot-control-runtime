#include "rcr/workbench/services/modbus_rtu.hpp"

#include <array>
#include <cstdio>

namespace rcr::workbench {

std::uint16_t
modbus_rtu_crc16(std::span<const std::uint8_t> data) noexcept {
  std::uint16_t crc = 0xFFFFu;
  for (const std::uint8_t byte : data) {
    crc ^= static_cast<std::uint16_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001u) != 0) {
        crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001u);
      } else {
        crc = static_cast<std::uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

std::string bytes_to_hex(std::span<const std::uint8_t> data) {
  std::string out;
  out.resize(data.size() * 2);
  for (std::size_t i = 0; i < data.size(); ++i) {
    std::array<char, 3> buf{};
    static_cast<void>(std::snprintf(buf.data(), buf.size(), "%02x", data[i]));
    out[i * 2] = buf[0];
    out[i * 2 + 1] = buf[1];
  }
  return out;
}

std::vector<std::uint8_t>
encode_rtu_read(std::uint8_t slave_id, std::uint8_t function,
                std::uint16_t address, std::uint16_t quantity) {
  std::vector<std::uint8_t> frame;
  frame.reserve(8);
  frame.push_back(slave_id);
  frame.push_back(function);
  frame.push_back(static_cast<std::uint8_t>((address >> 8) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(address & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>((quantity >> 8) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(quantity & 0xFFu));
  const auto crc = modbus_rtu_crc16(frame);
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFFu));
  return frame;
}

Result<ModbusRtuReadBits>
decode_rtu_read_bits(std::span<const std::uint8_t> frame,
                     std::uint8_t expected_slave,
                     std::uint8_t expected_function) {
  if (frame.size() < 5) {
    return Error{Errc::InvalidArgument, "RTU reply shorter than 5 bytes"};
  }
  const auto crc_got = static_cast<std::uint16_t>(
      frame[frame.size() - 2] |
      (static_cast<std::uint16_t>(frame[frame.size() - 1]) << 8));
  const auto crc_calc = modbus_rtu_crc16(frame.first(frame.size() - 2));
  if (crc_got != crc_calc) {
    return Error{Errc::Rejected, "RTU CRC mismatch"};
  }
  if (frame[0] != expected_slave) {
    return Error{Errc::Rejected, "RTU slave id mismatch"};
  }

  ModbusRtuReadBits decoded;
  decoded.slave_id = frame[0];
  decoded.function = frame[1];
  if ((decoded.function & 0x80u) != 0) {
    decoded.exception = true;
    decoded.exception_code = frame.size() > 2 ? frame[2] : 0;
    return decoded;
  }
  if (decoded.function != expected_function) {
    return Error{Errc::Rejected, "RTU function mismatch"};
  }
  if (frame.size() < 6) {
    return Error{Errc::InvalidArgument, "RTU bit reply missing data"};
  }
  const std::uint8_t byte_count = frame[2];
  if (byte_count < 1 || frame.size() != static_cast<std::size_t>(5 + byte_count)) {
    return Error{Errc::InvalidArgument, "RTU bit reply length mismatch"};
  }
  decoded.bit_count = 8;
  decoded.bit_byte = frame[3];
  return decoded;
}

std::vector<std::uint8_t>
encode_rtu_write_single_coil(std::uint8_t slave_id, std::uint16_t address,
                             bool on) {
  std::vector<std::uint8_t> frame;
  frame.reserve(8);
  frame.push_back(slave_id);
  frame.push_back(kModbusFnWriteSingleCoil);
  frame.push_back(static_cast<std::uint8_t>((address >> 8) & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>(address & 0xFFu));
  frame.push_back(on ? 0xFFu : 0x00u);
  frame.push_back(0x00u);
  const auto crc = modbus_rtu_crc16(frame);
  frame.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  frame.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFFu));
  return frame;
}

Result<ModbusRtuWriteCoil>
decode_rtu_write_single_coil(std::span<const std::uint8_t> frame,
                             std::uint8_t expected_slave,
                             std::uint16_t expected_address, bool expected_on) {
  if (frame.size() < 5) {
    return Error{Errc::InvalidArgument, "RTU write reply shorter than 5 bytes"};
  }
  const auto crc_got = static_cast<std::uint16_t>(
      frame[frame.size() - 2] |
      (static_cast<std::uint16_t>(frame[frame.size() - 1]) << 8));
  const auto crc_calc = modbus_rtu_crc16(frame.first(frame.size() - 2));
  if (crc_got != crc_calc) {
    return Error{Errc::Rejected, "RTU CRC mismatch"};
  }
  if (frame[0] != expected_slave) {
    return Error{Errc::Rejected, "RTU slave id mismatch"};
  }

  ModbusRtuWriteCoil decoded;
  decoded.slave_id = frame[0];
  decoded.function = frame[1];
  if ((decoded.function & 0x80u) != 0) {
    decoded.exception = true;
    decoded.exception_code = frame.size() > 2 ? frame[2] : 0;
    return decoded;
  }
  if (decoded.function != kModbusFnWriteSingleCoil) {
    return Error{Errc::Rejected, "RTU function mismatch"};
  }
  if (frame.size() != 8) {
    return Error{Errc::InvalidArgument, "RTU FC05 reply length mismatch"};
  }
  decoded.address = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(frame[2]) << 8) | frame[3]);
  decoded.on = frame[4] == 0xFFu && frame[5] == 0x00u;
  if (decoded.address != expected_address) {
    return Error{Errc::Rejected, "RTU FC05 address mismatch"};
  }
  if (decoded.on != expected_on) {
    return Error{Errc::Rejected, "RTU FC05 value mismatch"};
  }
  return decoded;
}

} // namespace rcr::workbench
