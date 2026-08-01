#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include <string>
#include <string_view>
#include <utility>

namespace rcr {

/// Runtime Core 的统一错误类别，避免底层模块通过异常跨越线程/控制边界。
/// Errc 用于程序分支；它不是 errno 的完整复制，底层 errno 只保留在诊断 message/stats。
enum class Errc : int {
  Ok = 0,
  InvalidArgument = 1,
  NotOpen = 2,
  IoError = 3,
  Timeout = 4,
  WouldBlock = 5,
  Busy = 6,
  Rejected = 7,
  Unsupported = 8,
};

[[nodiscard]] constexpr std::string_view to_string(Errc code) noexcept {
  switch (code) {
    case Errc::Ok:
      return "OK";
    case Errc::InvalidArgument:
      return "INVALID_ARGUMENT";
    case Errc::NotOpen:
      return "NOT_OPEN";
    case Errc::IoError:
      return "IO_ERROR";
    case Errc::Timeout:
      return "TIMEOUT";
    case Errc::WouldBlock:
      return "WOULD_BLOCK";
    case Errc::Busy:
      return "BUSY";
    case Errc::Rejected:
      return "REJECTED";
    case Errc::Unsupported:
      return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

/// 错误码用于程序分支，message 只用于诊断，不应通过字符串匹配参与控制决策。
/// message 使用 std::string，构造时可能分配，因此 Error/Result 不构成无分配硬实时通道。
class Error {
 public:
  Error() = default;
  explicit Error(Errc code, std::string message = {})
      : code_(code), message_(std::move(message)) {}

  [[nodiscard]] Errc code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] explicit operator bool() const noexcept { return code_ != Errc::Ok; }

 private:
  Errc code_{Errc::Ok};
  std::string message_{};
};

template <typename T>
class Result {
 public:
  // Result 用显式返回值表达成功或失败，调用方必须先检查 ok()/operator bool 再访问 value。
  // 这是仓内最小实现：错误状态仍默认构造一个 T，因此要求 T 可默认构造；它不是通用
  // std::expected 替代，也不会阻止调用方在错误状态误用 value()。
  Result(T value) : value_(std::move(value)), error_() {}  // NOLINT
  Result(Error error) : value_(), error_(std::move(error)) {}  // NOLINT

  [[nodiscard]] bool ok() const noexcept { return !error_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  /// 成功时返回 Ok Error；调用方通常只在 !ok() 后读取 message。
  [[nodiscard]] const Error& error() const noexcept { return error_; }
  [[nodiscard]] T& value() & { return value_; }
  [[nodiscard]] const T& value() const& { return value_; }
  [[nodiscard]] T&& value() && { return std::move(value_); }

 private:
  T value_{};
  Error error_{};
};

template <>
class Result<void> {
 public:
  Result() = default;
  Result(Error error) : error_(std::move(error)) {}  // NOLINT

  [[nodiscard]] bool ok() const noexcept { return !error_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] const Error& error() const noexcept { return error_; }

  static Result success() { return Result{}; }

 private:
  Error error_{};
};

}  // namespace rcr
