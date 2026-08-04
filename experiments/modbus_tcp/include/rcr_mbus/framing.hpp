#pragma once

// 流组帧：解决“TCP 是字节流，不是报文边界”的问题。
// 一次 recv 可能只给半个 MBAP（半包）、半个 body，或一次给多帧（粘包）。
// 备选：按固定长度切——不行，PDU 变长；靠休眠定界——那是 RTU 的事，TCP 靠 Length。

#include "rcr_mbus/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rcr::mbus {

// 状态机直觉：缓冲追加 → 够 7 字节读 Length → 等到 total=6+Length → 弹出完整 ADU 字节。
// NeedMore = 继续等；Invalid* = 坏流，上层应关连接（不要在坏 length 上死循环）。
class StreamFramer {
 public:
  void append(std::span<const std::uint8_t> chunk);

  // Ok + value：得到一帧原始 ADU 字节；NeedMore：继续 recv；其它：致命协议错误。
  Result<std::vector<std::uint8_t>> try_pop_adu();

  void clear();
  std::size_t buffered() const { return buffer_.size(); }

 private:
  std::vector<std::uint8_t> buffer_;
};

}  // namespace rcr::mbus
