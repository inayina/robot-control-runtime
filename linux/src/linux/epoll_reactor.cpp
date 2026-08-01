// Linux I/O 层：封装 epoll 注册和等待，不解释业务协议。
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

EpollReactor::EpollReactor() : epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)) {
  // EPOLL_CLOEXEC 防止未来 daemon exec 子进程时泄漏 reactor fd；构造不抛异常，
  // 创建失败保留 -1，调用方通过 valid()/Result 观察。
}

EpollReactor::~EpollReactor() {
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
  }
}

EpollReactor::EpollReactor(EpollReactor&& other) noexcept : epoll_fd_(other.epoll_fd_) {
  // epoll fd 与普通 fd 一样只能有一个关闭责任；移动后源对象必须失效。
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
  // data union 只存 fd，保持封装简单。未来若存对象指针，必须同时证明指针生命周期
  // 长于注册期；当前做法避免 readiness 返回悬空指针。
  event.events = events;
  event.data.fd = fd;
  // Linux 对 EPOLL_CTL_DEL 忽略 event 参数，传 nullptr 明确表达“只删除注册关系”。
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

  // epoll_wait 需要调用方提供连续输出数组。该 vector 每次 wait 分配，当前 I/O 规模可接受；
  // 若 benchmark 证明分配影响周期，应在 I/O 层复用缓冲区，而不是提前复杂化封装。
  std::vector<epoll_event> kernel_events(max_events);
  int ready = 0;
  do {
    // 信号可能以 EINTR 中断等待。这里重试同一个相对 timeout，极端情况下总等待会略超
    // 原超时；未来 signalfd 将目标信号纳入 fd 路径后，这种干扰会进一步减少。
    ready = ::epoll_wait(epoll_fd_, kernel_events.data(), static_cast<int>(max_events),
                         static_cast<int>(timeout_count));
  } while (ready < 0 && errno == EINTR);
  if (ready < 0) {
    return epoll_error("epoll_wait");
  }

  // 只复制内核实际填充的 ready 个槽位；events 原样上交，调用方负责 ERR/HUP/IN 顺序。
  std::vector<ReadyFd> result;
  result.reserve(static_cast<std::size_t>(ready));
  for (int index = 0; index < ready; ++index) {
    result.push_back(ReadyFd{kernel_events[static_cast<std::size_t>(index)].data.fd,
                             kernel_events[static_cast<std::size_t>(index)].events});
  }
  return result;
}

}  // namespace rcr
