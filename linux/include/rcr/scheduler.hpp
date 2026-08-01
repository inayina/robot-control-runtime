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
  /// 两次计划唤醒边界之间的间隔，必须大于 0；Runtime 默认 10 ms（100 Hz）。
  /// 这是监督频率，不是 CPU 时间配额，也不代表 callback 一定能在 period 内完成。
  std::chrono::nanoseconds period{std::chrono::milliseconds{10}};
  /// 1..99 请求当前 worker 使用 Linux SCHED_FIFO；0 保持继承到的普通调度策略。
  /// 调度属性属于线程而非整个进程，所以必须由新建 worker 自己申请。
  int fifo_priority{0};
  /// 为 true 时，SCHED_FIFO 设置失败将阻止线程进入周期循环；为 false 时继续运行，
  /// 但 fifo_error 必须保留真实 errno，调用方不能把降级误报成 FIFO 成功。
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
  /// 已经进入 callback 的次数；启动但尚未到第一个期限时为 0。
  std::uint64_t cycles{0};
  /// callback 完成时已经跨过的周期边界总数；一次严重过载可能增加多个 miss。
  std::uint64_t deadline_misses{0};
  /// lateness 只测“实际唤醒 - 计划唤醒”，不包含 callback 执行时间。
  std::int64_t min_lateness_ns{0};
  std::int64_t max_lateness_ns{0};
  std::int64_t mean_lateness_ns{0};
  /// 只有 pthread_setschedparam 真正成功才为 true，不能从请求优先级推断。
  bool fifo_enabled{false};
  /// pthread_setschedparam 返回的错误号；0 表示未请求或申请成功。
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
 *
 * 生命周期合同：start 创建线程并等待其完成调度策略/时钟初始化；request_stop 只发布
 * 停止意图，不负责回收线程；join 才等待线程退出。当前 clock_nanosleep 不由 eventfd
 * 唤醒，因此 stop 最坏需等待到本周期绝对期限，界限约为一个 period。
 */
class PeriodicScheduler {
 public:
  using TickCallback = std::function<void(const SchedulerTick&)>;

  explicit PeriodicScheduler(SchedulerConfig config = {});
  ~PeriodicScheduler();

  PeriodicScheduler(const PeriodicScheduler&) = delete;
  PeriodicScheduler& operator=(const PeriodicScheduler&) = delete;

  /// 启动并完成同步握手。返回成功时 worker 已完成启动检查，running() 为 true。
  /// 同一对象在旧线程 join 前重复 start 返回 Busy；join 后允许重新 start，统计会清零。
  Result<void> start(TickCallback callback);
  /// 可由其他线程调用；只设置原子停止标志，不阻塞，也不执行 join。
  void request_stop() noexcept;
  /// 等待 worker 退出；对未启动或已经 join 的对象是幂等操作。
  void join();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] SchedulerStats stats() const noexcept;
  [[nodiscard]] const SchedulerConfig& config() const noexcept;

 private:
  void run(TickCallback callback);

  // config_ 在构造后只读；worker 启动后不得并发修改周期或 FIFO 参数。
  SchedulerConfig config_;
  // thread_ 是否 joinable 同时表达“是否还有必须回收的 std::thread 资源”。
  std::thread thread_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> running_{false};

  // std::thread 构造成功不等于 worker 初始化成功。以下条件变量把 FIFO/时钟初始化结果
  // 从 worker 交还 start()，避免 start() 先返回成功、随后 worker 才因权限失败退出。
  mutable std::mutex startup_mutex_;
  std::condition_variable startup_cv_;
  bool startup_done_{false};
  Error startup_error_{};

  // 统计项允许读到不同瞬间的近似快照，只用于诊断，不参与控制决策，所以使用 relaxed。
  // running/stop_requested 承担跨线程生命周期发布，单独使用 acquire/release。
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
