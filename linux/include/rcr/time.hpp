#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"

#include <cstdint>
#include <time.h>

namespace rcr {

/// 读取 CLOCK_MONOTONIC，返回自启动以来的纳秒数；不受系统校时和时区变化影响。
[[nodiscard]] Result<std::int64_t> monotonic_now_ns() noexcept;

/// timespec 与纳秒之间的无损转换，输入必须是非负单调时间。
[[nodiscard]] timespec ns_to_timespec(std::int64_t nanoseconds) noexcept;
[[nodiscard]] std::int64_t timespec_to_ns(const timespec& value) noexcept;

}  // namespace rcr
