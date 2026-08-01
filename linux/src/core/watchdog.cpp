// Runtime Core：基于单调时钟的命令 watchdog，不等同于硬件 watchdog。
#include "rcr/watchdog.hpp"

#include <algorithm>

namespace rcr {

MonotonicWatchdog::MonotonicWatchdog(std::chrono::nanoseconds timeout) : timeout_(timeout) {}

void MonotonicWatchdog::arm(std::int64_t now_ns) noexcept {
  // 先写基准和 expired，最后发布 armed=true；观察到 armed 的检查方才应解释 last_kick。
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
  // 先关闭监督，再清锁存。Runtime 的 state_mutex 提供组合串行化；原子操作本身主要
  // 保证无 data race 和对诊断读取的可见性。
  armed_.store(false, std::memory_order_release);
  expired_.store(false, std::memory_order_release);
}

WatchdogCheck MonotonicWatchdog::check(std::int64_t now_ns) noexcept {
  if (!armed_.load(std::memory_order_acquire)) {
    return {};
  }
  const std::int64_t last_ns = last_kick_ns_.load(std::memory_order_acquire);
  // 理论上同一单调时钟不会倒退；clamp 到 0 是防御错误输入/采样边界，不能把负 age
  // 解释为“更健康的命令”。
  const std::int64_t age_ns = std::max<std::int64_t>(now_ns - last_ns, 0);
  if (age_ns < timeout_.count()) {
    return WatchdogCheck{WatchdogState::Healthy, false, age_ns};
  }
  // exchange 同时读取旧锁存并写入 true，只有第一个越过期限的检查返回 newly_expired。
  const bool was_expired = expired_.exchange(true, std::memory_order_acq_rel);
  return WatchdogCheck{WatchdogState::Expired, !was_expired, age_ns};
}

std::chrono::nanoseconds MonotonicWatchdog::timeout() const noexcept { return timeout_; }

}  // namespace rcr
