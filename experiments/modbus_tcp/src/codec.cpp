#include "rcr_mbus/codec.hpp"

namespace rcr::mbus {
namespace {

Result<std::vector<std::uint8_t>> pdu_too_short() {
  return {Error::Truncated, {}, "pdu too short"};
}

}  // namespace

Result<std::vector<std::uint8_t>> encode_adu(const Adu& adu) {
  if (adu.pdu.empty()) {
    return {Error::InvalidLength, {}, "empty pdu"};
  }
  if (adu.pdu.size() > kMaxPduSize) {
    return {Error::InvalidLength, {}, "pdu too large"};
  }
  if (adu.mbap.protocol_id != kProtocolId) {
    return {Error::InvalidProtocolId, {}, "protocol_id must be 0"};
  }

  // Length 只覆盖 UnitID + PDU，不含 Trans/Proto/Length 自身那 6 字节。
  Adu copy = adu;
  copy.mbap.length = static_cast<std::uint16_t>(1 + copy.pdu.size());

  // 读这段写出的字节：TID(2) PID(2) LEN(2) UNIT(1) | PDU...
  std::vector<std::uint8_t> out;
  out.reserve(kMbapSize + copy.pdu.size());
  put_u16_be(out, copy.mbap.transaction_id);
  put_u16_be(out, copy.mbap.protocol_id);
  put_u16_be(out, copy.mbap.length);
  out.push_back(copy.mbap.unit_id);
  out.insert(out.end(), copy.pdu.begin(), copy.pdu.end());
  return {Error::Ok, std::move(out), {}};
}

Result<Adu> decode_adu(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kMbapSize) {
    return {Error::Truncated, {}, "adu shorter than MBAP"};
  }
  Adu adu;
  auto tid = get_u16_be(bytes, 0);
  auto pid = get_u16_be(bytes, 2);
  auto len = get_u16_be(bytes, 4);
  if (!tid || !pid || !len) {
    return {Error::Truncated, {}, "mbap truncated"};
  }
  adu.mbap.transaction_id = tid.value;
  adu.mbap.protocol_id = pid.value;
  adu.mbap.length = len.value;
  adu.mbap.unit_id = bytes[6];

  if (adu.mbap.protocol_id != kProtocolId) {
    return {Error::InvalidProtocolId, {}, "protocol_id != 0"};
  }
  // length 含 unit_id + PDU；至少 unit_id + function → length≥2
  if (adu.mbap.length < 2) {
    return {Error::InvalidLength, {}, "length < 2"};
  }
  // ADU 总长 = 6 + Length（前 6 字节固定 + Length 所指 Unit+PDU）
  const std::size_t expected = 6u + static_cast<std::size_t>(adu.mbap.length);
  if (expected > kMaxAduSize) {
    return {Error::InvalidLength, {}, "adu exceeds max"};
  }
  if (bytes.size() < expected) {
    return {Error::Truncated, {}, "adu incomplete"};
  }
  if (bytes.size() != expected) {
    // 刻意要求“精确一帧”：多出来的字节应由 StreamFramer 先切走，避免静默吞粘包。
    return {Error::InvalidLength, {}, "adu size mismatch (caller must pass exact frame)"};
  }
  const std::size_t pdu_len = static_cast<std::size_t>(adu.mbap.length) - 1u;
  adu.pdu.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kMbapSize),
                 bytes.begin() + static_cast<std::ptrdiff_t>(kMbapSize + pdu_len));
  if (adu.pdu.empty()) {
    return {Error::InvalidLength, {}, "empty pdu after unit"};
  }
  return {Error::Ok, std::move(adu), {}};
}

Result<std::vector<std::uint8_t>> encode_read_holding_request(std::uint16_t address,
                                                             std::uint16_t quantity) {
  // 规范把单次读上限钉在 125，挡住离谱 qty 吃带宽 / 超 PDU。
  if (quantity < 1 || quantity > 125) {
    return {Error::IllegalValue, {}, "read quantity out of range"};
  }
  // 读这段：03 | addr_hi addr_lo | qty_hi qty_lo
  std::vector<std::uint8_t> pdu;
  pdu.push_back(kFcReadHolding);
  put_u16_be(pdu, address);
  put_u16_be(pdu, quantity);
  return {Error::Ok, std::move(pdu), {}};
}

Result<std::vector<std::uint8_t>> encode_write_single_request(std::uint16_t address,
                                                             std::uint16_t value) {
  // 读这段：06 | addr | value（请求与成功响应同形，便于对照抓包）
  std::vector<std::uint8_t> pdu;
  pdu.push_back(kFcWriteSingle);
  put_u16_be(pdu, address);
  put_u16_be(pdu, value);
  return {Error::Ok, std::move(pdu), {}};
}

Result<std::vector<std::uint8_t>> encode_write_multiple_request(
    std::uint16_t address, std::span<const std::uint16_t> values) {
  if (values.empty() || values.size() > 123) {
    return {Error::IllegalValue, {}, "write multiple quantity out of range"};
  }
  const auto byte_count = static_cast<std::uint8_t>(values.size() * 2);
  // 读这段：10 | addr | qty | byte_count | reg0..regN（每个 reg 2B 大端）
  std::vector<std::uint8_t> pdu;
  pdu.push_back(kFcWriteMultiple);
  put_u16_be(pdu, address);
  put_u16_be(pdu, static_cast<std::uint16_t>(values.size()));
  pdu.push_back(byte_count);
  for (std::uint16_t v : values) {
    put_u16_be(pdu, v);
  }
  return {Error::Ok, std::move(pdu), {}};
}

Result<std::vector<std::uint8_t>> encode_read_holding_response(
    std::span<const std::uint16_t> values) {
  if (values.empty() || values.size() > 125) {
    return {Error::IllegalValue, {}, "response quantity out of range"};
  }
  // 读这段：03 | byte_count(=2*N) | values...
  std::vector<std::uint8_t> pdu;
  pdu.push_back(kFcReadHolding);
  pdu.push_back(static_cast<std::uint8_t>(values.size() * 2));
  for (std::uint16_t v : values) {
    put_u16_be(pdu, v);
  }
  return {Error::Ok, std::move(pdu), {}};
}

Result<std::vector<std::uint8_t>> encode_write_single_response(std::uint16_t address,
                                                              std::uint16_t value) {
  return encode_write_single_request(address, value);
}

Result<std::vector<std::uint8_t>> encode_write_multiple_response(std::uint16_t address,
                                                                std::uint16_t quantity) {
  // 成功只回地址与数量，不回寄存器内容（省带宽、合同固定 5 字节 PDU）。
  std::vector<std::uint8_t> pdu;
  pdu.push_back(kFcWriteMultiple);
  put_u16_be(pdu, address);
  put_u16_be(pdu, quantity);
  return {Error::Ok, std::move(pdu), {}};
}

Result<std::vector<std::uint8_t>> encode_exception_response(std::uint8_t function,
                                                           std::uint8_t exception_code) {
  // 读这段：把请求 function 的最高位置 1（|0x80），后跟 1 字节 exception code。
  // 客户端应先看 bit7，再谈正常响应布局。
  std::vector<std::uint8_t> pdu;
  pdu.push_back(static_cast<std::uint8_t>(function | 0x80u));
  pdu.push_back(exception_code);
  return {Error::Ok, std::move(pdu), {}};
}

Result<ReadHoldingResponse> decode_read_holding_response(std::span<const std::uint8_t> pdu) {
  if (pdu.size() < 2) {
    return pdu_too_short().error == Error::Truncated
               ? Result<ReadHoldingResponse>{Error::Truncated, {}, "pdu too short"}
               : Result<ReadHoldingResponse>{Error::Truncated, {}, "pdu too short"};
  }
  if (pdu[0] != kFcReadHolding) {
    return {Error::UnexpectedPdu, {}, "not read holding response"};
  }
  const std::uint8_t byte_count = pdu[1];
  if (byte_count % 2 != 0) {
    return {Error::IllegalValue, {}, "odd byte count"};
  }
  if (pdu.size() != 2u + byte_count) {
    return {Error::Truncated, {}, "byte_count mismatch"};
  }
  ReadHoldingResponse resp;
  resp.values.reserve(byte_count / 2);
  for (std::size_t i = 0; i < byte_count; i += 2) {
    auto v = get_u16_be(pdu, 2 + i);
    if (!v) {
      return {v.error, {}, v.message};
    }
    resp.values.push_back(v.value);
  }
  return {Error::Ok, std::move(resp), {}};
}

Result<WriteSingleResponse> decode_write_single_response(std::span<const std::uint8_t> pdu) {
  if (pdu.size() != 5 || pdu[0] != kFcWriteSingle) {
    return {Error::UnexpectedPdu, {}, "not write single response"};
  }
  auto a = get_u16_be(pdu, 1);
  auto v = get_u16_be(pdu, 3);
  if (!a || !v) {
    return {Error::Truncated, {}, "write single truncated"};
  }
  return {Error::Ok, WriteSingleResponse{a.value, v.value}, {}};
}

Result<WriteMultipleResponse> decode_write_multiple_response(std::span<const std::uint8_t> pdu) {
  if (pdu.size() != 5 || pdu[0] != kFcWriteMultiple) {
    return {Error::UnexpectedPdu, {}, "not write multiple response"};
  }
  auto a = get_u16_be(pdu, 1);
  auto q = get_u16_be(pdu, 3);
  if (!a || !q) {
    return {Error::Truncated, {}, "write multiple truncated"};
  }
  return {Error::Ok, WriteMultipleResponse{a.value, q.value}, {}};
}

Result<ExceptionInfo> decode_exception_response(std::span<const std::uint8_t> pdu) {
  // 恰好 2 字节且 function 最高位为 1。
  if (pdu.size() != 2 || (pdu[0] & 0x80u) == 0) {
    return {Error::UnexpectedPdu, {}, "not exception response"};
  }
  return {Error::Ok, ExceptionInfo{static_cast<std::uint8_t>(pdu[0] & 0x7Fu), pdu[1]}, {}};
}

}  // namespace rcr::mbus
