// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/scheduler.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
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

RCR_TEST_MAIN()
