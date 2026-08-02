// Orange Pi/Linux Runtime：非周期路径的分位数统计。
#include "rcr/stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace rcr {

Result<std::int64_t> percentile_ns(std::span<const std::int64_t> samples, double percentile) {
  if (samples.empty()) {
    return Error{Errc::InvalidArgument, "percentile requires at least one sample"};
  }
  if (!(percentile >= 0.0) || percentile > 100.0 || !std::isfinite(percentile)) {
    return Error{Errc::InvalidArgument, "percentile must be in [0, 100]"};
  }
  std::vector<std::int64_t> sorted(samples.begin(), samples.end());
  std::sort(sorted.begin(), sorted.end());
  if (sorted.size() == 1) {
    return sorted.front();
  }
  const double index = percentile / 100.0 * static_cast<double>(sorted.size() - 1);
  const std::size_t lo = static_cast<std::size_t>(std::floor(index));
  const std::size_t hi = static_cast<std::size_t>(std::ceil(index));
  if (lo == hi) {
    return sorted[lo];
  }
  const double frac = index - static_cast<double>(lo);
  const double mixed = static_cast<double>(sorted[lo]) * (1.0 - frac) +
                       static_cast<double>(sorted[hi]) * frac;
  return static_cast<std::int64_t>(std::llround(mixed));
}

Result<PercentileSummary> summarize_lateness_ns(std::span<const std::int64_t> samples) {
  if (samples.empty()) {
    return Error{Errc::InvalidArgument, "summary requires at least one sample"};
  }
  PercentileSummary out{};
  out.count = samples.size();
  out.min_ns = *std::min_element(samples.begin(), samples.end());
  out.max_ns = *std::max_element(samples.begin(), samples.end());
  const long double sum =
      std::accumulate(samples.begin(), samples.end(), 0.0L);
  out.mean_ns = static_cast<std::int64_t>(
      std::llround(sum / static_cast<long double>(samples.size())));

  auto assign = [&](double p, std::int64_t& slot) -> Result<void> {
    auto value = percentile_ns(samples, p);
    if (!value) {
      return value.error();
    }
    slot = value.value();
    return Result<void>::success();
  };
  if (auto rc = assign(50.0, out.p50_ns); !rc) {
    return rc.error();
  }
  if (auto rc = assign(95.0, out.p95_ns); !rc) {
    return rc.error();
  }
  if (auto rc = assign(99.0, out.p99_ns); !rc) {
    return rc.error();
  }
  if (auto rc = assign(99.9, out.p99_9_ns); !rc) {
    return rc.error();
  }
  return out;
}

}  // namespace rcr
