#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// CAN I/O 线程：拥有 SocketCan 与 epoll，不复制状态机。

#include "rcr/can_bus.hpp"
#include "rcr/epoll_reactor.hpp"
#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"
#include "rcr/runtime.hpp"
#include "rcr/runtime_events.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace rcr {

enum class IoStopReason : std::uint8_t {
  None = 0,
  EventFd = 1,
  Signal = 2,
  IoError = 3,
  SendFailure = 4,
};

[[nodiscard]] constexpr std::string_view to_string(IoStopReason reason) noexcept {
  switch (reason) {
    case IoStopReason::None:
      return "NONE";
    case IoStopReason::EventFd:
      return "EVENTFD";
    case IoStopReason::Signal:
      return "SIGNAL";
    case IoStopReason::IoError:
      return "IO_ERROR";
    case IoStopReason::SendFailure:
      return "SEND_FAILURE";
  }
  return "UNKNOWN";
}

struct CanIoStats {
  std::uint64_t frames_received{0};
  std::uint64_t frames_sent{0};
  std::uint64_t decode_rejects{0};
  std::uint64_t queue_rejects{0};
  std::uint64_t wakeups{0};
  IoStopReason stop_reason{IoStopReason::None};
  int last_errno{0};
};

struct CanIoConfig {
  std::string can_if{"vcan0"};
  std::uint8_t node_id{1};
  /// 单次 epoll 唤醒最多处理的 CAN 帧，避免饿死停止事件。
  std::size_t max_frames_per_wake{32};
  int cpu_affinity{-1};  // <0 表示不绑定
};

/**
 * 单一 I/O 线程：epoll(SocketCAN, eventfd, signalfd)。
 *
 * Owner：本类拥有 SocketCan 与 EpollReactor；eventfd/signalfd 由 Daemon 拥有，这里只
 * 保存非 owning 句柄。解码后的事件按值写入 BoundedInputQueue；输出从 LinuxRuntime
 * mailbox 取出后再检查 Active/session/deadline 并编码发送。
 *
 * 备选：阻塞 receive 线程 + 独立 signal 线程。不选，因为增加共享状态和关闭竞态，
 * 而 V1 只有一个 CAN fd。
 */
class CanIoLoop {
 public:
  CanIoLoop(CanIoConfig config, LinuxRuntime& runtime, BoundedInputQueue& queue,
            EventFd& stop_event, SignalFd& signals);
  ~CanIoLoop();

  CanIoLoop(const CanIoLoop&) = delete;
  CanIoLoop& operator=(const CanIoLoop&) = delete;

  /// 打开 CAN、注册 epoll、启动 I/O 线程。失败时不留下 joinable 线程。
  [[nodiscard]] Result<void> start();
  /// 仅通过 eventfd 请求停止；不 join。
  void request_stop();
  /// 等待 I/O 线程退出；幂等。
  void join();

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] CanIoStats stats() const;
  [[nodiscard]] IoStopReason stop_reason() const noexcept;

 private:
  void thread_main();
  [[nodiscard]] Result<void> setup_locked();
  void teardown_fds();
  void handle_can_ready();
  void handle_stop_event();
  void handle_signal();
  void pump_output();
  [[nodiscard]] bool push_event(RuntimeInputEvent event);
  [[nodiscard]] Result<void> maybe_set_affinity();

  CanIoConfig config_;
  LinuxRuntime& runtime_;
  BoundedInputQueue& queue_;
  EventFd& stop_event_;
  SignalFd& signals_;

  EpollReactor reactor_{};
  SocketCan bus_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> startup_done_{false};
  std::atomic<IoStopReason> stop_reason_{IoStopReason::None};
  std::atomic<std::uint64_t> frames_received_{0};
  std::atomic<std::uint64_t> frames_sent_{0};
  std::atomic<std::uint64_t> decode_rejects_{0};
  std::atomic<std::uint64_t> queue_rejects_{0};
  std::atomic<std::uint64_t> wakeups_{0};
  std::atomic<int> last_errno_{0};
};

}  // namespace rcr
