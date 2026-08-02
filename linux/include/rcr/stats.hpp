#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// 分位数在非周期上下文计算；周期 callback 只写预分配样本槽。

#include "rcr/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rcr {

struct PercentileSummary {
  std::size_t count{0};
  std::int64_t min_ns{0};
  std::int64_t max_ns{0};
  std::int64_t mean_ns{0};
  std::int64_t p50_ns{0};
  std::int64_t p95_ns{0};
  std::int64_t p99_ns{0};
  std::int64_t p99_9_ns{0};
};

/**
 * 线性插值分位数（单位保持与输入相同，benchmark 使用 ns）。
 *
 * 算法：对升序样本，令 index = p/100 * (N-1)。取 floor/ceil 两端点线性插值。
 * N==0 → InvalidArgument；N==1 → 所有分位等于该样本。
 * p 必须落在 [0, 100]。不修改调用方传入的未排序视图：内部复制后再排序。
 *
 * 备选：周期内直方图。首版不选，避免量化边界争议；原始样本更易审查与复算。
 */
[[nodiscard]] Result<std::int64_t> percentile_ns(std::span<const std::int64_t> samples,
                                                 double percentile);

[[nodiscard]] Result<PercentileSummary> summarize_lateness_ns(
    std::span<const std::int64_t> samples);

/// 人类可读算法名，写入证据 summary，保证跨工具可追溯。
[[nodiscard]] constexpr std::string_view percentile_algorithm_id() noexcept {
  return "linear_interpolation_index_p_over_100_times_n_minus_1";
}

}  // namespace rcr
