// Linux 机制层：通过 POSIX clock_gettime 采样 CLOCK_MONOTONIC，并完成纳秒换算。
#include "rcr/time.hpp"

#include <cerrno>
#include <cstring>
#include <string>

namespace rcr {

Result<std::int64_t> monotonic_now_ns() noexcept {
  timespec now{};
  // clock_gettime 失败时 errno 只在当前线程有效，立即复制到诊断字符串；不能退化到
  // CLOCK_REALTIME，因为墙钟跳变会破坏 deadline/watchdog 的同一时间域。
  if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return Error{Errc::IoError,
                 std::string("clock_gettime(CLOCK_MONOTONIC): ") + std::strerror(errno)};
  }
  return timespec_to_ns(now);
}

timespec ns_to_timespec(std::int64_t nanoseconds) noexcept {
  timespec value{};
  // POSIX timespec 把秒和不足一秒的纳秒分开；输入合同为非负，因此余数天然落在规范范围。
  value.tv_sec = static_cast<time_t>(nanoseconds / 1'000'000'000LL);
  value.tv_nsec = static_cast<long>(nanoseconds % 1'000'000'000LL);
  return value;
}

std::int64_t timespec_to_ns(const timespec& value) noexcept {
  // 先提升到 int64 再乘，避免 time_t/long 的中间表达式按较窄类型计算。
  return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000LL +
         static_cast<std::int64_t>(value.tv_nsec);
}

}  // namespace rcr
