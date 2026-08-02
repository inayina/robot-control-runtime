// 分位数单测：固定输入可复算，不依赖主机调度。
#include "rcr/stats.hpp"
#include "test_support.hpp"

#include <vector>

RCR_TEST(PercentileEmptyRejected) {
  std::vector<std::int64_t> empty;
  RCR_EXPECT(!rcr::percentile_ns(empty, 50.0).ok());
}

RCR_TEST(PercentileSingleSample) {
  std::vector<std::int64_t> samples{42};
  auto p50 = rcr::percentile_ns(samples, 50.0);
  auto p99 = rcr::percentile_ns(samples, 99.0);
  RCR_REQUIRE(p50.ok());
  RCR_REQUIRE(p99.ok());
  RCR_EXPECT(p50.value() == 42);
  RCR_EXPECT(p99.value() == 42);
}

RCR_TEST(PercentileLinearInterpolationKnownSet) {
  // 0,10,20,30,40 → p50 = index 2.0 → 20；p25 = index 1.0 → 10
  std::vector<std::int64_t> samples{40, 10, 30, 0, 20};
  auto p0 = rcr::percentile_ns(samples, 0.0);
  auto p25 = rcr::percentile_ns(samples, 25.0);
  auto p50 = rcr::percentile_ns(samples, 50.0);
  auto p100 = rcr::percentile_ns(samples, 100.0);
  RCR_REQUIRE(p0.ok() && p25.ok() && p50.ok() && p100.ok());
  RCR_EXPECT(p0.value() == 0);
  RCR_EXPECT(p25.value() == 10);
  RCR_EXPECT(p50.value() == 20);
  RCR_EXPECT(p100.value() == 40);
}

RCR_TEST(SummarizeMatchesComponents) {
  std::vector<std::int64_t> samples{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto summary = rcr::summarize_lateness_ns(samples);
  RCR_REQUIRE(summary.ok());
  RCR_EXPECT(summary.value().count == 10);
  RCR_EXPECT(summary.value().min_ns == 1);
  RCR_EXPECT(summary.value().max_ns == 10);
  RCR_EXPECT(summary.value().p50_ns == rcr::percentile_ns(samples, 50.0).value());
  RCR_EXPECT(summary.value().p95_ns == rcr::percentile_ns(samples, 95.0).value());
  RCR_EXPECT(summary.value().p99_ns == rcr::percentile_ns(samples, 99.0).value());
  RCR_EXPECT(summary.value().p99_9_ns == rcr::percentile_ns(samples, 99.9).value());
}

RCR_TEST_MAIN()
