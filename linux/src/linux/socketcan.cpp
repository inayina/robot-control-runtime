// Linux I/O 层：SocketCAN raw socket 的 RAII 与非阻塞帧收发。
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
  // errno 在下一次 libc/syscall 调用后可能改变，所以调用方先按值捕获，再在这里格式化。
  return Error{Errc::IoError, std::string(prefix) + ": " + std::strerror(err)};
}

can_frame to_kernel_frame(const CanFrame& frame) {
  // Linux UAPI 的 can_frame 与项目 CanFrame 是边界两侧的不同类型，必须显式复制字段。
  // 内核经典 CAN 帧最多携带 8 字节；公开 send 接口会先拒绝超长输入，这里的 clamp
  // 只是防御式边界，不能把非法长度静默当成合法发送行为。
  can_frame out{};
  out.can_id = frame.can_id;
  out.can_dlc = frame.len > 8 ? 8 : frame.len;
  std::memcpy(out.data, frame.data, out.can_dlc);
  return out;
}

CanFrame from_kernel_frame(const can_frame& frame) {
  // 保留 can_id 中 Linux 定义的 EFF/RTR/ERR flags，由更上层 CAN V1 codec 决定是否拒绝；
  // SocketCan 只负责安全搬运内核帧，不在传输层偷做协议语义判断。
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
  // 移动的是 fd 的关闭责任，不是复制一个可被双方关闭的整数句柄。
  other.fd_ = -1;
}

SocketCan& SocketCan::operator=(SocketCan&& other) noexcept {
  if (this != &other) {
    // 目标对象可能已经拥有另一个 socket，必须先关闭旧资源再接管新 fd。
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
    // 重复 open 是幂等成功，不重建 socket，避免悄悄更换已注册到 epoll 的 fd。
    return Result<void>::success();
  }

  // PF_CAN 选择 CAN 协议族，SOCK_RAW/CAN_RAW 暴露经典 CAN 帧。返回的 fd 先放局部变量：
  // fd 在 bind 全部成功后才写入成员；中途失败由本函数关闭，避免对象处于半打开状态。
  const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    return make_io_error("socket(PF_CAN)", errno);
  }

  ifreq ifr{};
  // bind 需要内核接口 index 而不是字符串名。SIOCGIFINDEX 将 vcan0/can0 解析成 ifindex；
  // snprintf 的目标固定为 IFNAMSIZ，超长或不存在的名称由 ioctl 失败路径处理。
  std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname_.c_str());
  if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
    const int err = errno;
    ::close(fd);
    return make_io_error("ioctl(SIOCGIFINDEX)", err);
  }

  sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  // bind 把这个 socket 限定到一个 CAN netdevice；不 bind 时的接收/发送语义不同，V1 不用。
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    const int err = errno;
    ::close(fd);
    return make_io_error("bind", err);
  }

  // 最后一步提交所有权。此后析构/close 负责关闭，局部失败路径不再触碰 fd。
  fd_ = fd;
  return Result<void>::success();
}

void SocketCan::close() {
  if (fd_ >= 0) {
    // close 的返回错误无法安全补救：fd 即使 close 报 EINTR，在 Linux 上也不应盲目重试，
    // 否则该数字若已复用可能误关别的资源。这里按 RAII 清理语义直接置为无效。
    ::close(fd_);
    fd_ = -1;
  }
}

Result<void> SocketCan::set_nonblocking(bool enabled) {
  if (!is_open()) {
    return Error{Errc::NotOpen, "SocketCan not open"};
  }
  // F_GETFL/F_SETFL 是 read-modify-write；必须保留其他已有状态位，只切换 O_NONBLOCK。
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

  // CAN raw socket 以一个完整 can_frame 为消息边界。write 成功应返回 sizeof(can_frame)，
  // 不能把短写当成“发送了部分 payload”继续补写，因为这不是字节流协议。
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
    // 非负超时通过 select 限制等待；0 是纯轮询，负值保留为明确允许阻塞读取。
    // select 只报告 readiness，不消费数据；真正的 read 仍可能因非阻塞竞态返回 WouldBlock。
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_, &readfds);

    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);

    // 第一个参数必须是“最大 fd + 1”，不是 fd 数量。这里只监视一个 socket。
    const int rc = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
    if (rc < 0) {
      return make_io_error("select", errno);
    }
    if (rc == 0) {
      return Error{Errc::Timeout, "receive timeout"};
    }
  }

  // SOCK_RAW CAN socket 的一次 read 对应一个帧。短读表示内核/API 行为不符合合同，
  // 不能将未填满的结构体交给 codec。
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
  // Fake 的 send 直接回环到同一对象队列；它没有进程隔离和内核路径，只适合类级单测。
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
