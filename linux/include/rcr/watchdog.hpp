#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include <atomic>
#include <chrono>
#include <cstdint>

namespace rcr {

enum class WatchdogState : std::uint8_t { Disarmed = 0, Healthy = 1, Expired = 2 };

struct WatchdogCheck {
  WatchdogState state{WatchdogState::Disarmed};
  /// 只在 Healthy/Disarmed 首次跨入 Expired 时为 true，用于防止周期线程重复投递迁移。
  bool newly_expired{false};
  /// 当前时间距最后一次 kick 的时间，单位纳秒；未启动时为 0。
  std::int64_t age_ns{0};
};

/**
 * 使用 CLOCK_MONOTONIC 时间戳的命令 watchdog。
 *
 * 原子变量防止字段级 data race，但 arm/kick/check 是由多个字段组成的逻辑操作；
 * LinuxRuntime 仍用 state_mutex 把 publish/kick 与 on_tick/check 串行化，保证组合语义。
 * watchdog 本身不执行回调或改变状态机；首次超时后的迁移由 LinuxRuntime 完成。
 *
 * timeout_ 在构造后只读，所有 now_ns 必须来自同一个 CLOCK_MONOTONIC 域。若错误地传入
 * 墙钟或其他设备时间，age 没有可比较意义。
 */
class MonotonicWatchdog {
 public:
  explicit MonotonicWatchdog(std::chrono::nanoseconds timeout);

  /// 开始监督，并把 now 作为第一条基准；即使尚无命令，到 timeout 也会过期。
  void arm(std::int64_t now_ns) noexcept;
  /// 接受一条合法新命令后刷新基准并清除过期锁存；不负责校验命令本身。
  void kick(std::int64_t now_ns) noexcept;
  /// 停止监督并清除过期锁存；last_kick 可保留，因为 disarmed 状态不会读取其语义。
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
