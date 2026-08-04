#include "rcr_mbus/framing.hpp"

#include "rcr_mbus/codec.hpp"

namespace rcr::mbus {

void StreamFramer::append(std::span<const std::uint8_t> chunk) {
  buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
}

void StreamFramer::clear() { buffer_.clear(); }

Result<std::vector<std::uint8_t>> StreamFramer::try_pop_adu() {
  // 半包路径 1：连 MBAP 都不够 → NeedMore，上层继续 recv。
  if (buffer_.size() < kMbapSize) {
    return {Error::NeedMore, {}, "wait mbap"};
  }
  // 只读定界字段；不在这里完整 decode（坏流与完整解析职责分开）。
  auto pid = get_u16_be(buffer_, 2);
  auto len = get_u16_be(buffer_, 4);
  if (!pid || !len) {
    return {Error::Truncated, {}, "mbap read failed"};
  }
  if (pid.value != kProtocolId) {
    return {Error::InvalidProtocolId, {}, "protocol_id != 0"};
  }
  if (len.value < 2) {
    return {Error::InvalidLength, {}, "length < 2"};
  }
  // ADU 总长 = 6 + Length（Length 覆盖 UnitID+PDU）
  const std::size_t total = 6u + static_cast<std::size_t>(len.value);
  if (total > kMaxAduSize) {
    // 致命：虚假超大 Length 不得继续缓冲等待（防资源耗尽）。
    return {Error::InvalidLength, {}, "length implies oversized adu"};
  }
  // 半包路径 2：头齐了但 body 不齐 → NeedMore。
  if (buffer_.size() < total) {
    return {Error::NeedMore, {}, "wait body"};
  }
  // 粘包：只弹出第一帧，剩余留在 buffer_ 供下次 try_pop。
  std::vector<std::uint8_t> frame(buffer_.begin(),
                                  buffer_.begin() + static_cast<std::ptrdiff_t>(total));
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
  return {Error::Ok, std::move(frame), {}};
}

}  // namespace rcr::mbus
