#pragma once

// 编解码层：只负责“结构体 ↔ 字节”，不碰 TCP。
// 为什么单独成模块：组帧（半包）与 socket 超时换实现时，PDU 合同仍可单测；
// 备选是把 encode 内嵌 client——难测坏 Length/exception，且与 libmodbus 对照更吵。

#include "rcr_mbus/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rcr::mbus {

// 大端读写：Modbus 寄存器与 MBAP 多字节字段均为 big-endian（MSB 先上线）。
// 读这段：高字节在前 → (b0<<8)|b1。
inline void put_u16_be(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

inline Result<std::uint16_t> get_u16_be(std::span<const std::uint8_t> b, std::size_t off) {
  if (off + 2 > b.size()) {
    return {Error::Truncated, 0, "need 2 bytes"};
  }
  const std::uint16_t v = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(b[off]) << 8) | static_cast<std::uint16_t>(b[off + 1]));
  return {Error::Ok, v, {}};
}

// 读这段字节（encode）：写出完整 ADU = MBAP7 + PDU；Length 由 PDU 自动填。
Result<std::vector<std::uint8_t>> encode_adu(const Adu& adu);

// 从**恰好一帧**的 ADU 字节解析字段（不含流组装；粘包请先用 StreamFramer）。
Result<Adu> decode_adu(std::span<const std::uint8_t> bytes);

// --- 请求 PDU 布局（function 后大端字段）---
// 0x03 Read Holding:  FC | addr(2) | qty(2)                 qty∈[1,125]
// 0x06 Write Single:  FC | addr(2) | value(2)
// 0x10 Write Multiple:FC | addr(2) | qty(2) | byte_count(1) | values(2*qty)
Result<std::vector<std::uint8_t>> encode_read_holding_request(std::uint16_t address,
                                                             std::uint16_t quantity);
Result<std::vector<std::uint8_t>> encode_write_single_request(std::uint16_t address,
                                                             std::uint16_t value);
Result<std::vector<std::uint8_t>> encode_write_multiple_request(
    std::uint16_t address, std::span<const std::uint16_t> values);

// --- 响应 PDU ---
// 0x03: FC | byte_count(1) | values...   （byte_count = 2*qty）
// 0x06: 与请求同形回显
// 0x10: FC | addr(2) | qty(2)            （不回写寄存器内容）
// 异常: (FC|0x80) | exception_code(1)
Result<std::vector<std::uint8_t>> encode_read_holding_response(
    std::span<const std::uint16_t> values);
Result<std::vector<std::uint8_t>> encode_write_single_response(std::uint16_t address,
                                                              std::uint16_t value);
Result<std::vector<std::uint8_t>> encode_write_multiple_response(std::uint16_t address,
                                                                std::uint16_t quantity);
Result<std::vector<std::uint8_t>> encode_exception_response(std::uint8_t function,
                                                           std::uint8_t exception_code);

struct ReadHoldingResponse {
  std::vector<std::uint16_t> values;
};
struct WriteSingleResponse {
  std::uint16_t address = 0;
  std::uint16_t value = 0;
};
struct WriteMultipleResponse {
  std::uint16_t address = 0;
  std::uint16_t quantity = 0;
};
struct ExceptionInfo {
  std::uint8_t function = 0;  // 已去掉 0x80 的原始 function
  std::uint8_t code = 0;
};

Result<ReadHoldingResponse> decode_read_holding_response(std::span<const std::uint8_t> pdu);
Result<WriteSingleResponse> decode_write_single_response(std::span<const std::uint8_t> pdu);
Result<WriteMultipleResponse> decode_write_multiple_response(std::span<const std::uint8_t> pdu);
Result<ExceptionInfo> decode_exception_response(std::span<const std::uint8_t> pdu);

}  // namespace rcr::mbus
