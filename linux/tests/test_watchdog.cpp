// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/watchdog.hpp"
#include "test_support.hpp"

#include <chrono>

RCR_TEST(WatchdogExpiresOnceUntilKick) {
  rcr::MonotonicWatchdog watchdog(std::chrono::milliseconds{10});
  watchdog.arm(1'000'000'000LL);

  auto check = watchdog.check(1'005'000'000LL);
  RCR_EXPECT(check.state == rcr::WatchdogState::Healthy);
  RCR_EXPECT(!check.newly_expired);

  check = watchdog.check(1'010'000'000LL);
  RCR_EXPECT(check.state == rcr::WatchdogState::Expired);
  RCR_EXPECT(check.newly_expired);
  RCR_EXPECT(!watchdog.check(1'011'000'000LL).newly_expired);

  watchdog.kick(1'012'000'000LL);
  RCR_EXPECT(watchdog.check(1'013'000'000LL).state == rcr::WatchdogState::Healthy);
}

RCR_TEST(DisarmedWatchdogCannotExpire) {
  rcr::MonotonicWatchdog watchdog(std::chrono::nanoseconds{1});
  RCR_EXPECT(watchdog.check(99).state == rcr::WatchdogState::Disarmed);
  watchdog.arm(100);
  watchdog.disarm();
  RCR_EXPECT(watchdog.check(1000).state == rcr::WatchdogState::Disarmed);
}

RCR_TEST_MAIN()
