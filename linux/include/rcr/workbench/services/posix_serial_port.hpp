#pragma once

// 阻塞 POSIX 串口：一次完整写+读事务。半双工方向由 HAT SP3485 硬件自动收发。
// 调用方必须在 worker/agent 线程使用；禁止放进 Qt GUI 线程。

#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rcr::workbench {

struct PosixSerialConfig {
  std::string device{"/dev/ttyS7"};
  std::uint32_t baud_rate{9600};
  char parity{'N'};
};

class PosixSerialPort {
public:
  [[nodiscard]] Result<void> open(const PosixSerialConfig &config);
  void close() noexcept;
  [[nodiscard]] bool is_open() const noexcept { return fd_.valid(); }
  [[nodiscard]] const std::string &device() const noexcept { return device_; }

  // 写完 drain，再按字符间隔收一帧。总超时有界；不会忙等。
  [[nodiscard]] Result<std::vector<std::uint8_t>>
  transact(std::span<const std::uint8_t> request,
           std::chrono::milliseconds timeout);

private:
  OwnedFd fd_{};
  std::string device_{};
};

} // namespace rcr::workbench
