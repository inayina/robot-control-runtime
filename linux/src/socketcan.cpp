// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/can_bus.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rcr {
namespace {

Error make_io_error(std::string_view prefix, int err) {
  return Error{Errc::IoError, std::string(prefix) + ": " + std::strerror(err)};
}

can_frame to_kernel_frame(const CanFrame& frame) {
  // 内核经典 CAN 帧最多携带 8 字节；公开 send 接口会先拒绝超长输入。
  can_frame out{};
  out.can_id = frame.can_id;
  out.can_dlc = frame.len > 8 ? 8 : frame.len;
  std::memcpy(out.data, frame.data, out.can_dlc);
  return out;
}

CanFrame from_kernel_frame(const can_frame& frame) {
  CanFrame out{};
  out.can_id = frame.can_id;
  out.len = frame.can_dlc > 8 ? 8 : frame.can_dlc;
  std::memcpy(out.data, frame.data, out.len);
  return out;
}

}  // namespace

SocketCan::SocketCan(std::string interface_name) : ifname_(std::move(interface_name)) {}

SocketCan::~SocketCan() { close(); }

SocketCan::SocketCan(SocketCan&& other) noexcept
    : ifname_(std::move(other.ifname_)), fd_(other.fd_) {
  other.fd_ = -1;
}

SocketCan& SocketCan::operator=(SocketCan&& other) noexcept {
  if (this != &other) {
    close();
    ifname_ = std::move(other.ifname_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

bool SocketCan::is_open() const noexcept { return fd_ >= 0; }

std::string_view SocketCan::interface_name() const noexcept { return ifname_; }

int SocketCan::native_handle() const noexcept { return fd_; }

Result<void> SocketCan::open() {
  if (ifname_.empty()) {
    return Error{Errc::InvalidArgument, "empty CAN interface name"};
  }
  if (is_open()) {
    return Result<void>::success();
  }

  // fd 在 bind 全部成功后才写入成员；中途失败由本函数关闭，避免半打开状态。
  const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    return make_io_error("socket(PF_CAN)", errno);
  }

  ifreq ifr{};
  std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname_.c_str());
  if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
    const int err = errno;
    ::close(fd);
    return make_io_error("ioctl(SIOCGIFINDEX)", err);
  }

  sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    const int err = errno;
    ::close(fd);
    return make_io_error("bind", err);
  }

  fd_ = fd;
  return Result<void>::success();
}

void SocketCan::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

Result<void> SocketCan::set_nonblocking(bool enabled) {
  if (!is_open()) {
    return Error{Errc::NotOpen, "SocketCan not open"};
  }
  int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags < 0) {
    return make_io_error("fcntl(F_GETFL)", errno);
  }
  if (enabled) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }
  if (::fcntl(fd_, F_SETFL, flags) < 0) {
    return make_io_error("fcntl(F_SETFL)", errno);
  }
  return Result<void>::success();
}

Result<void> SocketCan::send(const CanFrame& frame) {
  if (!is_open()) {
    return Error{Errc::NotOpen, "SocketCan not open"};
  }
  if (frame.len > 8) {
    return Error{Errc::InvalidArgument, "CAN frame length > 8"};
  }

  const can_frame kframe = to_kernel_frame(frame);
  const ssize_t n = ::write(fd_, &kframe, sizeof(kframe));
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Error{Errc::WouldBlock, "send would block"};
    }
    return make_io_error("write", errno);
  }
  if (static_cast<std::size_t>(n) != sizeof(kframe)) {
    return Error{Errc::IoError, "short CAN write"};
  }
  return Result<void>::success();
}

Result<CanFrame> SocketCan::receive(std::chrono::milliseconds timeout) {
  if (!is_open()) {
    return Error{Errc::NotOpen, "SocketCan not open"};
  }

  if (timeout.count() >= 0) {
    // 非负超时通过 select 限制等待；负值保留为调用方明确要求的阻塞读取。
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_, &readfds);

    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);

    const int rc = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
    if (rc < 0) {
      return make_io_error("select", errno);
    }
    if (rc == 0) {
      return Error{Errc::Timeout, "receive timeout"};
    }
  }

  can_frame kframe{};
  const ssize_t n = ::read(fd_, &kframe, sizeof(kframe));
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Error{Errc::WouldBlock, "receive would block"};
    }
    return make_io_error("read", errno);
  }
  if (static_cast<std::size_t>(n) != sizeof(kframe)) {
    return Error{Errc::IoError, "short CAN read"};
  }
  return from_kernel_frame(kframe);
}

FakeCanBus::FakeCanBus(std::string interface_name) : ifname_(std::move(interface_name)) {}

bool FakeCanBus::is_open() const noexcept { return open_; }

std::string_view FakeCanBus::interface_name() const noexcept { return ifname_; }

Result<void> FakeCanBus::open() {
  open_ = true;
  return Result<void>::success();
}

void FakeCanBus::close() {
  open_ = false;
  rx_queue_.clear();
}

Result<void> FakeCanBus::send(const CanFrame& frame) {
  if (!open_) {
    return Error{Errc::NotOpen, "FakeCanBus not open"};
  }
  if (frame.len > 8) {
    return Error{Errc::InvalidArgument, "CAN frame length > 8"};
  }
  rx_queue_.push_back(frame);
  return Result<void>::success();
}

Result<CanFrame> FakeCanBus::receive(std::chrono::milliseconds /*timeout*/) {
  // FakeCanBus 不模拟时间，空队列立即返回 Timeout，保证单元测试可重复。
  if (!open_) {
    return Error{Errc::NotOpen, "FakeCanBus not open"};
  }
  if (rx_queue_.empty()) {
    return Error{Errc::Timeout, "FakeCanBus queue empty"};
  }
  CanFrame frame = rx_queue_.front();
  rx_queue_.erase(rx_queue_.begin());
  return frame;
}

std::size_t FakeCanBus::queued() const noexcept { return rx_queue_.size(); }

void FakeCanBus::clear() { rx_queue_.clear(); }

}  // namespace rcr
