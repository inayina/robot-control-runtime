// Linux I/O 层：表达 fd 唯一所有权以及 eventfd/signalfd 停止唤醒原语。
#include "rcr/owned_fd.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace rcr {
namespace {

Error make_io_error(std::string_view prefix, int err) {
  return Error{Errc::IoError, std::string(prefix) + ": " + std::strerror(err)};
}

Error make_pthread_error(std::string_view prefix, int rc) {
  return Error{Errc::IoError, std::string(prefix) + ": " + std::strerror(rc)};
}

}  // namespace

void OwnedFd::reset() noexcept {
  if (fd_ >= 0) {
    // Linux 上 close 即使返回 EINTR 也不应盲目重试，以免复用后的 fd 被误关。
    ::close(fd_);
    fd_ = -1;
  }
}

void OwnedFd::reset(int fd) noexcept {
  reset();
  fd_ = fd;
}

int OwnedFd::release() noexcept {
  const int out = fd_;
  fd_ = -1;
  return out;
}

Result<EventFd> EventFd::create() {
  // EFD_CLOEXEC 避免 fork/exec 验收子进程继承停止 fd；EFD_NONBLOCK 配合 epoll。
  const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd < 0) {
    return make_io_error("eventfd", errno);
  }
  return EventFd{OwnedFd{fd}};
}

Result<void> EventFd::signal_stop() {
  if (!fd_.valid()) {
    return Error{Errc::NotOpen, "EventFd not open"};
  }
  // eventfd 计数以 u64 写入；值 1 表示一次停止请求。
  const std::uint64_t one = 1;
  for (;;) {
    const ssize_t n = ::write(fd_.get(), &one, sizeof(one));
    if (n == static_cast<ssize_t>(sizeof(one))) {
      return Result<void>::success();
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // 计数接近最大值时 write 会 EAGAIN；停止意图已经成立，无需失败。
      return Result<void>::success();
    }
    if (n < 0) {
      return make_io_error("eventfd write", errno);
    }
    return Error{Errc::IoError, "short eventfd write"};
  }
}

Result<std::uint64_t> EventFd::drain() {
  if (!fd_.valid()) {
    return Error{Errc::NotOpen, "EventFd not open"};
  }
  std::uint64_t total = 0;
  for (;;) {
    std::uint64_t value = 0;
    const ssize_t n = ::read(fd_.get(), &value, sizeof(value));
    if (n == static_cast<ssize_t>(sizeof(value))) {
      total += value;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return total;
    }
    if (n < 0) {
      return make_io_error("eventfd read", errno);
    }
    return Error{Errc::IoError, "short eventfd read"};
  }
}

Result<SignalFd> SignalFd::block_and_open_shutdown_signals() {
  // 必须先 block，再创建 signalfd，且后续线程继承该 mask；否则信号可能被默认处置。
  sigset_t mask{};
  ::sigemptyset(&mask);
  ::sigaddset(&mask, SIGINT);
  ::sigaddset(&mask, SIGTERM);
  sigset_t previous_mask{};
  const int mask_rc = ::pthread_sigmask(SIG_BLOCK, &mask, &previous_mask);
  if (mask_rc != 0) {
    return make_pthread_error("pthread_sigmask(SIG_BLOCK)", mask_rc);
  }
  const int fd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (fd < 0) {
    const int saved_errno = errno;
    // 工厂失败也必须撤销已经完成的 mask 修改，否则一次失败会污染调用线程后续行为。
    const int restore_rc = ::pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
    if (restore_rc != 0) {
      return make_pthread_error("signalfd failed and restoring signal mask", restore_rc);
    }
    return make_io_error("signalfd", saved_errno);
  }
  return SignalFd{OwnedFd{fd}, previous_mask, ::pthread_self()};
}

SignalFd::~SignalFd() { reset(); }

SignalFd::SignalFd(SignalFd&& other) noexcept
    : fd_(std::move(other.fd_)),
      previous_mask_(other.previous_mask_),
      owner_thread_(other.owner_thread_),
      restore_pending_(other.restore_pending_) {
  other.restore_pending_ = false;
}

SignalFd& SignalFd::operator=(SignalFd&& other) noexcept {
  if (this != &other) {
    reset();
    fd_ = std::move(other.fd_);
    previous_mask_ = other.previous_mask_;
    owner_thread_ = other.owner_thread_;
    restore_pending_ = other.restore_pending_;
    other.restore_pending_ = false;
  }
  return *this;
}

void SignalFd::reset() noexcept {
  fd_.reset();
  if (restore_pending_ && ::pthread_equal(::pthread_self(), owner_thread_)) {
    // 析构不能返回错误；显式需要诊断时调用 close_and_restore()。
    (void)::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
  }
  restore_pending_ = false;
}

Result<void> SignalFd::close_and_restore() {
  fd_.reset();
  if (!restore_pending_) {
    return Result<void>::success();
  }
  if (!::pthread_equal(::pthread_self(), owner_thread_)) {
    // signal mask 是线程属性；在别的线程恢复会修改错误的线程。
    return Error{Errc::Rejected, "SignalFd must restore its mask on the creating thread"};
  }
  const int rc = ::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
  if (rc != 0) {
    return make_pthread_error("pthread_sigmask(SIG_SETMASK)", rc);
  }
  restore_pending_ = false;
  return Result<void>::success();
}

Result<std::uint32_t> SignalFd::drain() {
  if (!fd_.valid()) {
    return Error{Errc::NotOpen, "SignalFd not open"};
  }
  std::uint32_t count = 0;
  for (;;) {
    signalfd_siginfo info{};
    const ssize_t n = ::read(fd_.get(), &info, sizeof(info));
    if (n == static_cast<ssize_t>(sizeof(info))) {
      ++count;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return count;
    }
    if (n < 0) {
      return make_io_error("signalfd read", errno);
    }
    return Error{Errc::IoError, "short signalfd read"};
  }
}

}  // namespace rcr
