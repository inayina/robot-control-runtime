#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include <atomic>
#include <chrono>
#include <cstdint>

namespace rcr {

enum class WatchdogState : std::uint8_t { Disarmed = 0, Healthy = 1, Expired = 2 };

struct WatchdogCheck {
  WatchdogState state{WatchdogState::Disarmed};
  bool newly_expired{false};
  /// 当前时间距最后一次 kick 的时间，单位纳秒；未启动时为 0。
  std::int64_t age_ns{0};
};

/**
 * 使用 CLOCK_MONOTONIC 时间戳的命令 watchdog。
 *
 * Application 线程可调用 arm/kick，周期线程调用 check。原子变量只传递时间戳和
 * 锁存状态，不执行回调；超时后的状态迁移由 LinuxRuntime 在周期线程中完成。
 */
class MonotonicWatchdog {
 public:
  explicit MonotonicWatchdog(std::chrono::nanoseconds timeout);

  void arm(std::int64_t now_ns) noexcept;
  void kick(std::int64_t now_ns) noexcept;
  void disarm() noexcept;
  [[nodiscard]] WatchdogCheck check(std::int64_t now_ns) noexcept;
  [[nodiscard]] std::chrono::nanoseconds timeout() const noexcept;

 private:
  std::chrono::nanoseconds timeout_;
  std::atomic<std::int64_t> last_kick_ns_{0};
  std::atomic<bool> armed_{false};
  std::atomic<bool> expired_{false};
};

}  // namespace rcr
