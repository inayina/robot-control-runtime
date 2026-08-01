#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// 唯一拥有一个 Linux fd 的可移动值类型：整数句柄本身不是 owner。

#include "rcr/result.hpp"

#include <cstdint>
#include <utility>

namespace rcr {

/**
 * 拥有单个 Linux 文件描述符的 RAII 值类型。
 *
 * 设计意图：eventfd/signalfd/timerfd/epoll 等生命周期若散落在裸 int 上，移动后容易
 * 双关或泄漏。本类型移动后源对象变为 -1；复制被删除。不使用 shared_ptr：V1 每个 fd
 * 只有一个明确 owner。
 *
 * 备选：每个子系统自写局部 RAII（node_sim 曾如此）。抽出公共类型是因为 daemon 需要在
 * 多个模块间转移 eventfd/signalfd 所有权，重复样板会增加漏关风险。
 */
class OwnedFd {
 public:
  OwnedFd() = default;
  explicit OwnedFd(int fd) noexcept : fd_(fd) {}
  ~OwnedFd() { reset(); }

  OwnedFd(const OwnedFd&) = delete;
  OwnedFd& operator=(const OwnedFd&) = delete;

  OwnedFd(OwnedFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  OwnedFd& operator=(OwnedFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

  /// 非 owning 观察句柄；调用方不得 close。
  [[nodiscard]] int native_handle() const noexcept { return fd_; }

  void reset() noexcept;
  /// 放弃关闭责任；调用后必须由新 owner 负责 close。
  [[nodiscard]] int release() noexcept;

  /// 用已打开的 fd 接管所有权；先前拥有的 fd 先关闭。
  void reset(int fd) noexcept;

 private:
  int fd_{-1};
};

/**
 * 非阻塞 eventfd：跨线程停止唤醒原语。
 *
 * 为什么不用 condition_variable：I/O 线程阻塞在 epoll_wait，cv 无法直接唤醒该 syscall。
 * 为什么不用 signal handler 里设 atomic：handler 可调用函数受限，且不能把停止纳入 epoll
 * 事件顺序。停止方只 write；I/O 线程 read 排空计数。重复 signal_stop 是安全的。
 */
class EventFd {
 public:
  EventFd() = default;

  EventFd(const EventFd&) = delete;
  EventFd& operator=(const EventFd&) = delete;
  EventFd(EventFd&&) noexcept = default;
  EventFd& operator=(EventFd&&) noexcept = default;

  /// 创建 EFD_NONBLOCK|EFD_CLOEXEC eventfd；失败不留下半初始化对象。
  [[nodiscard]] static Result<EventFd> create();

  [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }
  [[nodiscard]] int native_handle() const noexcept { return fd_.native_handle(); }

  /// 写入一次停止计数；EAGAIN 表示计数已饱和，仍视为已请求停止。
  [[nodiscard]] Result<void> signal_stop();
  /// 排空可读计数直到 EAGAIN；返回累计读到的值（诊断用）。
  [[nodiscard]] Result<std::uint64_t> drain();

 private:
  explicit EventFd(OwnedFd fd) : fd_(std::move(fd)) {}
  OwnedFd fd_{};
};

/**
 * 把已屏蔽信号变成可读 fd，供 epoll 统一处理。
 *
 * 合同：调用 create 前，当前线程必须已经 pthread_sigmask 阻塞对应信号，且新建线程
 * 继承该 mask，否则信号可能仍走默认/旧 handler，signalfd 读不到。
 */
class SignalFd {
 public:
  SignalFd() = default;

  SignalFd(const SignalFd&) = delete;
  SignalFd& operator=(const SignalFd&) = delete;
  SignalFd(SignalFd&&) noexcept = default;
  SignalFd& operator=(SignalFd&&) noexcept = default;

  /// 阻塞 SIGINT/SIGTERM 并创建非阻塞 signalfd。
  [[nodiscard]] static Result<SignalFd> block_and_open_shutdown_signals();

  [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }
  [[nodiscard]] int native_handle() const noexcept { return fd_.native_handle(); }

  /// 排空待处理信号；返回读到的信号次数（每个 signalfd_siginfo 计 1）。
  [[nodiscard]] Result<std::uint32_t> drain();

 private:
  explicit SignalFd(OwnedFd fd) : fd_(std::move(fd)) {}
  OwnedFd fd_{};
};

}  // namespace rcr
