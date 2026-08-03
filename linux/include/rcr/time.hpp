#pragma once

// Linux 机制层：CLOCK_MONOTONIC 采样与 timespec 换算；不是 MCU 共享协议头。

#include "rcr/result.hpp"

#include <cstdint>
#include <time.h>

namespace rcr {

/// 读取 CLOCK_MONOTONIC，返回自某个未指定起点以来的纳秒数；只应用于同机同一启动期内
/// 的差值/排序，不受系统校时和时区变化影响，也不能作为跨机器线协议时间戳。
[[nodiscard]] Result<std::int64_t> monotonic_now_ns() noexcept;

/// timespec 与纳秒之间的转换；本项目输入是非负单调时间并假设乘加不溢出 int64。
/// ns_to_timespec 产生满足 0 <= tv_nsec < 1e9 的规范化 timespec。
[[nodiscard]] timespec ns_to_timespec(std::int64_t nanoseconds) noexcept;
[[nodiscard]] std::int64_t timespec_to_ns(const timespec& value) noexcept;

}  // namespace rcr
