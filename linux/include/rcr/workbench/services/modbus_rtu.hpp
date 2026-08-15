#pragma once

// Modbus RTU 线级编解码：CRC-16/IBM、读离散输入/线圈。不打开串口，不依赖 Qt。
// 地址与功能码以 2026-08-15 Orange Pi live probe 为准，不是入库的厂商手册。

#include "rcr/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rcr::workbench {

inline constexpr std::uint8_t kModbusFnReadCoils = 0x01;
inline constexpr std::uint8_t kModbusFnReadDiscreteInputs = 0x02;
inline constexpr std::uint8_t kModbusFnWriteSingleCoil = 0x05;
inline constexpr std::uint16_t kMr0IoRegisterStart = 0;
inline constexpr std::uint16_t kMr0IoBitQuantity = 8;

[[nodiscard]] std::uint16_t
modbus_rtu_crc16(std::span<const std::uint8_t> data) noexcept;

[[nodiscard]] std::string bytes_to_hex(std::span<const std::uint8_t> data);

[[nodiscard]] std::vector<std::uint8_t>
encode_rtu_read(std::uint8_t slave_id, std::uint8_t function,
                std::uint16_t address, std::uint16_t quantity);

struct ModbusRtuReadBits {
  std::uint8_t slave_id{0};
  std::uint8_t function{0};
  std::uint8_t bit_count{0};
  std::uint8_t bit_byte{0};
  bool exception{false};
  std::uint8_t exception_code{0};
};

[[nodiscard]] Result<ModbusRtuReadBits>
decode_rtu_read_bits(std::span<const std::uint8_t> frame,
                     std::uint8_t expected_slave,
                     std::uint8_t expected_function);

[[nodiscard]] std::vector<std::uint8_t>
encode_rtu_write_single_coil(std::uint8_t slave_id, std::uint16_t address,
                             bool on);

struct ModbusRtuWriteCoil {
  std::uint8_t slave_id{0};
  std::uint8_t function{0};
  std::uint16_t address{0};
  bool on{false};
  bool exception{false};
  std::uint8_t exception_code{0};
};

[[nodiscard]] Result<ModbusRtuWriteCoil>
decode_rtu_write_single_coil(std::span<const std::uint8_t> frame,
                             std::uint8_t expected_slave,
                             std::uint16_t expected_address, bool expected_on);

} // namespace rcr::workbench
