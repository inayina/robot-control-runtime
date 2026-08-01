// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/watchdog.hpp"

#include <algorithm>

namespace rcr {

MonotonicWatchdog::MonotonicWatchdog(std::chrono::nanoseconds timeout) : timeout_(timeout) {}

void MonotonicWatchdog::arm(std::int64_t now_ns) noexcept {
  last_kick_ns_.store(now_ns, std::memory_order_release);
  expired_.store(false, std::memory_order_release);
  armed_.store(true, std::memory_order_release);
}

void MonotonicWatchdog::kick(std::int64_t now_ns) noexcept {
  // 先发布时间戳再清除锁存，周期线程不会把旧 kick 误认为刚恢复的新命令。
  last_kick_ns_.store(now_ns, std::memory_order_release);
  expired_.store(false, std::memory_order_release);
}

void MonotonicWatchdog::disarm() noexcept {
  armed_.store(false, std::memory_order_release);
  expired_.store(false, std::memory_order_release);
}

WatchdogCheck MonotonicWatchdog::check(std::int64_t now_ns) noexcept {
  if (!armed_.load(std::memory_order_acquire)) {
    return {};
  }
  const std::int64_t last_ns = last_kick_ns_.load(std::memory_order_acquire);
  const std::int64_t age_ns = std::max<std::int64_t>(now_ns - last_ns, 0);
  if (age_ns < timeout_.count()) {
    return WatchdogCheck{WatchdogState::Healthy, false, age_ns};
  }
  const bool was_expired = expired_.exchange(true, std::memory_order_acq_rel);
  return WatchdogCheck{WatchdogState::Expired, !was_expired, age_ns};
}

std::chrono::nanoseconds MonotonicWatchdog::timeout() const noexcept { return timeout_; }

}  // namespace rcr
