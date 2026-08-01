// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/time.hpp"

#include <cerrno>
#include <cstring>
#include <string>

namespace rcr {

Result<std::int64_t> monotonic_now_ns() noexcept {
  timespec now{};
  if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return Error{Errc::IoError,
                 std::string("clock_gettime(CLOCK_MONOTONIC): ") + std::strerror(errno)};
  }
  return timespec_to_ns(now);
}

timespec ns_to_timespec(std::int64_t nanoseconds) noexcept {
  timespec value{};
  value.tv_sec = static_cast<time_t>(nanoseconds / 1'000'000'000LL);
  value.tv_nsec = static_cast<long>(nanoseconds % 1'000'000'000LL);
  return value;
}

std::int64_t timespec_to_ns(const timespec& value) noexcept {
  return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000LL +
         static_cast<std::int64_t>(value.tv_nsec);
}

}  // namespace rcr
