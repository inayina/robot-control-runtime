#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rcr {

struct SchedulerConfig {
  /// 调度周期，必须大于 0；Runtime 默认 10 ms（100 Hz）。
  std::chrono::nanoseconds period{std::chrono::milliseconds{10}};
  /// 1..99 请求 Linux SCHED_FIFO；0 保持继承到的普通调度策略。
  int fifo_priority{0};
  /// 为 true 时，SCHED_FIFO 设置失败将阻止线程进入周期循环。
  bool require_fifo{false};
};

struct SchedulerTick {
  std::uint64_t sequence{0};
  /// 本周期的绝对唤醒目标，CLOCK_MONOTONIC 纳秒。
  std::int64_t scheduled_ns{0};
  /// callback 开始前采样的实际唤醒时间，CLOCK_MONOTONIC 纳秒。
  std::int64_t actual_ns{0};
  /// max(actual_ns - scheduled_ns, 0)，单位纳秒。
  std::int64_t wakeup_lateness_ns{0};
};

struct SchedulerStats {
  std::uint64_t cycles{0};
  /// callback 完成时已经跨过的周期边界总数。
  std::uint64_t deadline_misses{0};
  std::int64_t min_lateness_ns{0};
  std::int64_t max_lateness_ns{0};
  std::int64_t mean_lateness_ns{0};
  bool fifo_enabled{false};
  int fifo_error{0};
  /// 周期线程运行阶段的 errno 风格错误；0 表示正常退出。
  int worker_error{0};
};

/**
 * Linux Runtime 的单周期线程。
 *
 * 线程以 CLOCK_MONOTONIC 计算绝对期限，并用 clock_nanosleep(TIMER_ABSTIME)
 * 等待，从而避免相对 sleep 把每次执行时间累积成长期漂移。callback 在该线程内
 * 串行执行，必须保持有界且不能做文件 I/O；业务算法不属于此监督线程。
 */
class PeriodicScheduler {
 public:
  using TickCallback = std::function<void(const SchedulerTick&)>;

  explicit PeriodicScheduler(SchedulerConfig config = {});
  ~PeriodicScheduler();

  PeriodicScheduler(const PeriodicScheduler&) = delete;
  PeriodicScheduler& operator=(const PeriodicScheduler&) = delete;

  Result<void> start(TickCallback callback);
  void request_stop() noexcept;
  void join();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] SchedulerStats stats() const noexcept;
  [[nodiscard]] const SchedulerConfig& config() const noexcept;

 private:
  void run(TickCallback callback);

  SchedulerConfig config_;
  std::thread thread_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> running_{false};

  // start() 等待工作线程完成调度策略设置，避免调用方误以为 SCHED_FIFO 已生效。
  mutable std::mutex startup_mutex_;
  std::condition_variable startup_cv_;
  bool startup_done_{false};
  Error startup_error_{};

  std::atomic<std::uint64_t> cycles_{0};
  std::atomic<std::uint64_t> deadline_misses_{0};
  std::atomic<std::int64_t> min_lateness_ns_{0};
  std::atomic<std::int64_t> max_lateness_ns_{0};
  std::atomic<std::int64_t> total_lateness_ns_{0};
  std::atomic<bool> fifo_enabled_{false};
  std::atomic<int> fifo_error_{0};
  std::atomic<int> worker_error_{0};
};

}  // namespace rcr
