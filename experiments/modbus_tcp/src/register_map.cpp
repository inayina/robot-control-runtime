#include "rcr_mbus/register_map.hpp"

namespace rcr::mbus {

HoldingMap::HoldingMap(std::size_t size) : regs_(size, 0) {}

Result<std::vector<std::uint16_t>> HoldingMap::read(std::uint16_t address,
                                                   std::uint16_t quantity) const {
  if (quantity < 1 || quantity > 125) {
    return {Error::IllegalValue, {}, "quantity out of range"};
  }
  const std::size_t start = address;
  const std::size_t end = start + static_cast<std::size_t>(quantity);
  if (end > regs_.size()) {
    return {Error::IllegalAddress, {}, "read past map"};
  }
  return {Error::Ok,
          std::vector<std::uint16_t>(regs_.begin() + static_cast<std::ptrdiff_t>(start),
                                     regs_.begin() + static_cast<std::ptrdiff_t>(end)),
          {}};
}

Result<bool> HoldingMap::write_single(std::uint16_t address, std::uint16_t value) {
  if (static_cast<std::size_t>(address) >= regs_.size()) {
    return {Error::IllegalAddress, false, "write past map"};
  }
  regs_[address] = value;
  return {Error::Ok, true, {}};
}

Result<bool> HoldingMap::write_multiple(std::uint16_t address,
                                        std::span<const std::uint16_t> values) {
  if (values.empty() || values.size() > 123) {
    return {Error::IllegalValue, false, "quantity out of range"};
  }
  const std::size_t start = address;
  const std::size_t end = start + values.size();
  if (end > regs_.size()) {
    return {Error::IllegalAddress, false, "write past map"};
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    regs_[start + i] = values[i];
  }
  return {Error::Ok, true, {}};
}

Result<std::vector<std::uint8_t>> HoldingMap::handle_pdu(std::span<const std::uint8_t> pdu) {
  if (pdu.empty()) {
    return {Error::Truncated, {}, "empty pdu"};
  }
  const std::uint8_t fc = pdu[0];
  if (fc == kFcReadHolding) {
    // 请求必须正好 5 字节；长度不对 → exception 03（Illegal Data Value），不断开 TCP。
    if (pdu.size() != 5) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    auto addr = get_u16_be(pdu, 1);
    auto qty = get_u16_be(pdu, 3);
    if (!addr || !qty) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    auto rd = read(addr.value, qty.value);
    if (rd.error == Error::IllegalAddress) {
      return encode_exception_response(fc, kExIllegalDataAddress);
    }
    if (rd.error == Error::IllegalValue) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    if (!rd) {
      return {rd.error, {}, rd.message};
    }
    return encode_read_holding_response(rd.value);
  }
  if (fc == kFcWriteSingle) {
    if (pdu.size() != 5) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    auto addr = get_u16_be(pdu, 1);
    auto val = get_u16_be(pdu, 3);
    if (!addr || !val) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    auto wr = write_single(addr.value, val.value);
    if (wr.error == Error::IllegalAddress) {
      return encode_exception_response(fc, kExIllegalDataAddress);
    }
    if (!wr) {
      return {wr.error, {}, wr.message};
    }
    return encode_write_single_response(addr.value, val.value);
  }
  if (fc == kFcWriteMultiple) {
    if (pdu.size() < 6) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    auto addr = get_u16_be(pdu, 1);
    auto qty = get_u16_be(pdu, 3);
    const std::uint8_t byte_count = pdu[5];
    if (!addr || !qty) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    // byte_count 必须 = qty*2，且总长与声明一致——防截断/注入脏数据半写入。
    if (byte_count != qty.value * 2 || pdu.size() != 6u + byte_count) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    std::vector<std::uint16_t> values;
    values.reserve(qty.value);
    for (std::uint16_t i = 0; i < qty.value; ++i) {
      auto v = get_u16_be(pdu, 6u + static_cast<std::size_t>(i) * 2u);
      if (!v) {
        return encode_exception_response(fc, kExIllegalDataValue);
      }
      values.push_back(v.value);
    }
    auto wr = write_multiple(addr.value, values);
    if (wr.error == Error::IllegalAddress) {
      return encode_exception_response(fc, kExIllegalDataAddress);
    }
    if (wr.error == Error::IllegalValue) {
      return encode_exception_response(fc, kExIllegalDataValue);
    }
    if (!wr) {
      return {wr.error, {}, wr.message};
    }
    return encode_write_multiple_response(addr.value, qty.value);
  }
  // 未实现的 function（含 Coil 等）→ Illegal Function，仍保持连接。
  return encode_exception_response(fc, kExIllegalFunction);
}

}  // namespace rcr::mbus
