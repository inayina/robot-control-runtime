// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/scheduler.hpp"
#include "test_support.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <sched.h>
#include <thread>

RCR_TEST(AbsoluteSchedulerRunsAndStops) {
  rcr::SchedulerConfig config{};
  config.period = std::chrono::milliseconds{1};
  rcr::PeriodicScheduler scheduler(config);
  std::atomic<std::uint64_t> callbacks{0};

  RCR_REQUIRE(scheduler.start([&callbacks](const rcr::SchedulerTick& tick) {
    RCR_EXPECT(tick.actual_ns >= tick.scheduled_ns);
    callbacks.fetch_add(1, std::memory_order_relaxed);
  }).ok());
  std::this_thread::sleep_for(std::chrono::milliseconds{15});
  scheduler.request_stop();
  scheduler.join();

  const auto stats = scheduler.stats();
  RCR_EXPECT(!scheduler.running());
  RCR_EXPECT(stats.cycles >= 5);
  RCR_EXPECT(stats.cycles == callbacks.load(std::memory_order_relaxed));
  RCR_EXPECT(stats.max_lateness_ns >= stats.min_lateness_ns);
}

RCR_TEST(SchedulerRejectsInvalidConfiguration) {
  rcr::SchedulerConfig config{};
  config.period = std::chrono::nanoseconds{0};
  rcr::PeriodicScheduler scheduler(config);
  const auto result = scheduler.start([](const rcr::SchedulerTick&) {});
  RCR_EXPECT(!result.ok());
  RCR_EXPECT(result.error().code() == rcr::Errc::InvalidArgument);
}

RCR_TEST(SchedulerRejectsOutOfRangeCpuAffinity) {
  rcr::SchedulerConfig config{};
  config.cpu_affinity = CPU_SETSIZE;
  rcr::PeriodicScheduler scheduler(config);
  const auto result = scheduler.start([](const rcr::SchedulerTick&) {});
  RCR_EXPECT(!result.ok());
  RCR_EXPECT(result.error().code() == rcr::Errc::InvalidArgument);
}

RCR_TEST(OptionalFifoDoesNotPretendSuccess) {
  rcr::SchedulerConfig config{};
  config.period = std::chrono::milliseconds{1};
  config.fifo_priority = 1;
  config.require_fifo = false;
  rcr::PeriodicScheduler scheduler(config);
  RCR_REQUIRE(scheduler.start([](const rcr::SchedulerTick&) {}).ok());
  scheduler.request_stop();
  scheduler.join();
  const auto stats = scheduler.stats();
  RCR_EXPECT(stats.fifo_enabled || stats.fifo_error != 0);
}

RCR_TEST(CallbackExceptionStopsWorkerFailClosed) {
  rcr::SchedulerConfig config{};
  config.period = std::chrono::milliseconds{1};
  rcr::PeriodicScheduler scheduler(config);
  RCR_REQUIRE(scheduler.start([](const rcr::SchedulerTick&) {
    throw std::runtime_error("test callback failure");
  }).ok());

  bool stopped = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (!scheduler.running()) {
      stopped = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  RCR_EXPECT(stopped);
  scheduler.join();
  const auto stats = scheduler.stats();
  RCR_EXPECT(stats.worker_error != 0);
  RCR_EXPECT(!scheduler.running());
}

RCR_TEST(RepeatStartRejectedAndRepeatStopIsSafe) {
  rcr::SchedulerConfig config{};
  config.period = std::chrono::milliseconds{1};
  rcr::PeriodicScheduler scheduler(config);
  RCR_REQUIRE(scheduler.start([](const rcr::SchedulerTick&) {}).ok());
  const auto second = scheduler.start([](const rcr::SchedulerTick&) {});
  RCR_EXPECT(!second.ok());
  RCR_EXPECT(second.error().code() == rcr::Errc::Busy);

  scheduler.request_stop();
  scheduler.join();
  scheduler.request_stop();
  scheduler.join();
  RCR_EXPECT(!scheduler.running());

  RCR_REQUIRE(scheduler.start([](const rcr::SchedulerTick&) {}).ok());
  scheduler.request_stop();
  scheduler.join();
}

RCR_TEST(OverloadSkipsMissedDeadlinesWithoutCatchUp) {
  // 1 ms 周期 + 3 ms callback：证明 miss 按跨过的边界累计，且 next 跳到未来绝对边界，
  // 不会把过期周期逐个补跑成追赶风暴。lateness 仍是唤醒延迟，不等于 3 ms 执行时间。
  rcr::SchedulerConfig config{};
  config.period = std::chrono::milliseconds{1};
  rcr::PeriodicScheduler scheduler(config);

  constexpr std::size_t kCapacity = 64;
  std::array<std::int64_t, kCapacity> scheduled_ns{};
  std::atomic<std::size_t> tick_count{0};

  RCR_REQUIRE(scheduler
                  .start([&](const rcr::SchedulerTick& tick) {
                    const std::size_t index =
                        tick_count.fetch_add(1, std::memory_order_relaxed);
                    if (index < scheduled_ns.size()) {
                      scheduled_ns[index] = tick.scheduled_ns;
                    }
                    // 人工过载：占用 worker 约 3 个 period，迫使 finished_ns 越过旧边界。
                    std::this_thread::sleep_for(std::chrono::milliseconds{3});
                  })
                  .ok());
  std::this_thread::sleep_for(std::chrono::milliseconds{40});
  scheduler.request_stop();
  scheduler.join();

  const auto stats = scheduler.stats();
  const std::size_t count = tick_count.load(std::memory_order_relaxed);
  RCR_EXPECT(stats.cycles == count);
  RCR_EXPECT(count >= 3);
  RCR_EXPECT(count < scheduled_ns.size());
  // 空载约 40 次；过载跳周期后应远少于 duration/period。
  RCR_EXPECT(count <= 20);
  // 每次 3 ms 回调至少跨过约 3 个 1 ms 边界；总 miss 应随 cycles 明显增长。
  RCR_EXPECT(stats.deadline_misses >= count * 2);
  RCR_EXPECT(stats.deadline_misses >= 6);

  const std::int64_t period_ns = config.period.count();
  for (std::size_t i = 1; i < count; ++i) {
    const std::int64_t gap = scheduled_ns[i] - scheduled_ns[i - 1];
    // 跳过旧边界后，相邻计划唤醒至少隔开 2 个 period；追赶补跑会退化为 gap==period。
    RCR_EXPECT(gap >= 2 * period_ns);
    RCR_EXPECT(gap % period_ns == 0);
  }
}

RCR_TEST_MAIN()
