// Linux 目标测试；不依赖 MCU 工具链或硬件烧录环境。
#include "rcr/trace.hpp"
#include "test_support.hpp"

RCR_TEST(RingKeepsNewestEventsInOrder) {
  rcr::TraceBuffer trace(3);
  for (std::int64_t value = 1; value <= 5; ++value) {
    trace.record(rcr::TraceEvent{value, rcr::TraceKind::SchedulerTick, value, 0});
  }
  const auto snapshot = trace.snapshot();
  RCR_REQUIRE(snapshot.size() == 3);
  RCR_EXPECT(snapshot[0].value_a == 3);
  RCR_EXPECT(snapshot[1].value_a == 4);
  RCR_EXPECT(snapshot[2].value_a == 5);
}

RCR_TEST(ZeroCapacityDropsWithoutBlocking) {
  rcr::TraceBuffer trace(0);
  trace.record(rcr::TraceEvent{});
  RCR_EXPECT(trace.snapshot().empty());
  RCR_EXPECT(trace.dropped() == 1);
}

RCR_TEST_MAIN()
