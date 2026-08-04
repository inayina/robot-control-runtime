#pragma once

// Holding Register 映像：教学用“从站内存”。
// 地址为零基 0..N-1；线上大端由 codec 负责，这里存主机端 uint16。
// 厂商手册里的 4xxxx 习惯编号 ≠ 本 map 下标。未实现 Coil / Discrete / Input Register。

#include "rcr_mbus/codec.hpp"
#include "rcr_mbus/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rcr::mbus {

class HoldingMap {
 public:
  explicit HoldingMap(std::size_t size = 64);

  std::size_t size() const { return regs_.size(); }

  Result<std::vector<std::uint16_t>> read(std::uint16_t address, std::uint16_t quantity) const;
  Result<bool> write_single(std::uint16_t address, std::uint16_t value);
  Result<bool> write_multiple(std::uint16_t address, std::span<const std::uint16_t> values);

  // 读请求 PDU → 正常响应 PDU 或 exception PDU（function|0x80）。
  // 边界：非法 function / 越界地址 / 坏 quantity 都走 exception，而不是关 TCP——
  // 让客户端能区分“从站拒绝”与“网络失败”。
  Result<std::vector<std::uint8_t>> handle_pdu(std::span<const std::uint8_t> pdu);

 private:
  std::vector<std::uint16_t> regs_;
};

}  // namespace rcr::mbus
