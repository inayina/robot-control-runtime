// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/epoll_reactor.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>

namespace rcr {
namespace {

Error epoll_error(std::string_view operation) {
  return Error{Errc::IoError, std::string(operation) + ": " + std::strerror(errno)};
}

}  // namespace

EpollReactor::EpollReactor() : epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)) {}

EpollReactor::~EpollReactor() {
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
  }
}

EpollReactor::EpollReactor(EpollReactor&& other) noexcept : epoll_fd_(other.epoll_fd_) {
  other.epoll_fd_ = -1;
}

EpollReactor& EpollReactor::operator=(EpollReactor&& other) noexcept {
  if (this != &other) {
    if (epoll_fd_ >= 0) {
      ::close(epoll_fd_);
    }
    epoll_fd_ = other.epoll_fd_;
    other.epoll_fd_ = -1;
  }
  return *this;
}

bool EpollReactor::valid() const noexcept { return epoll_fd_ >= 0; }

Result<void> EpollReactor::control(int operation, int fd, std::uint32_t events) {
  if (!valid()) {
    return Error{Errc::NotOpen, "epoll instance is not open"};
  }
  if (fd < 0) {
    return Error{Errc::InvalidArgument, "monitored fd must be non-negative"};
  }
  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  epoll_event* event_ptr = operation == EPOLL_CTL_DEL ? nullptr : &event;
  if (::epoll_ctl(epoll_fd_, operation, fd, event_ptr) != 0) {
    return epoll_error("epoll_ctl");
  }
  return Result<void>::success();
}

Result<void> EpollReactor::add(int fd, std::uint32_t events) {
  return control(EPOLL_CTL_ADD, fd, events);
}

Result<void> EpollReactor::modify(int fd, std::uint32_t events) {
  return control(EPOLL_CTL_MOD, fd, events);
}

Result<void> EpollReactor::remove(int fd) { return control(EPOLL_CTL_DEL, fd, 0); }

Result<std::vector<ReadyFd>> EpollReactor::wait(std::chrono::milliseconds timeout,
                                                std::size_t max_events) {
  if (!valid()) {
    return Error{Errc::NotOpen, "epoll instance is not open"};
  }
  if (max_events == 0 ||
      max_events > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return Error{Errc::InvalidArgument, "max_events is outside epoll_wait range"};
  }
  const auto timeout_count = timeout.count();
  if (timeout_count < -1) {
    return Error{Errc::InvalidArgument, "epoll timeout must be -1 or non-negative"};
  }
  if (timeout_count > std::numeric_limits<int>::max()) {
    return Error{Errc::InvalidArgument, "epoll timeout is too large"};
  }

  std::vector<epoll_event> kernel_events(max_events);
  int ready = 0;
  do {
    ready = ::epoll_wait(epoll_fd_, kernel_events.data(), static_cast<int>(max_events),
                         static_cast<int>(timeout_count));
  } while (ready < 0 && errno == EINTR);
  if (ready < 0) {
    return epoll_error("epoll_wait");
  }

  std::vector<ReadyFd> result;
  result.reserve(static_cast<std::size_t>(ready));
  for (int index = 0; index < ready; ++index) {
    result.push_back(ReadyFd{kernel_events[static_cast<std::size_t>(index)].data.fd,
                             kernel_events[static_cast<std::size_t>(index)].events});
  }
  return result;
}

}  // namespace rcr
