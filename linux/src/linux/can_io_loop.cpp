// Linux I/O 层：拥有 SocketCAN epoll 循环，并把协议事件按值交给 Core。
#include "rcr/can_io_loop.hpp"

#include "rcr/can_v1.hpp"
#include "rcr/time.hpp"

#include <cerrno>
#include <cstring>

#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace rcr {
namespace {

Result<can_v1::WireOutputCommand> to_wire_command(const OutputCommand &cmd,
                                                  std::uint8_t node_id,
                                                  std::int64_t now_ns) {
  if (cmd.session_id == 0 || cmd.session_id > 0xFFFFu || cmd.sequence == 0 ||
      (cmd.mask & 0xFFu) == 0) {
    return Error{Errc::Rejected,
                 "output command fields exceed CAN V1 wire width"};
  }
  auto validity = can_v1::validity_10ms_from_deadline(now_ns, cmd.deadline_ns);
  if (!validity) {
    return validity.error();
  }
  can_v1::WireOutputCommand wire{};
  wire.node_id = node_id;
  wire.mask = static_cast<std::uint8_t>(cmd.mask & 0xFFu);
  wire.session_id = static_cast<std::uint16_t>(cmd.session_id);
  // Runtime 内部 sequence 是不回绕的 u64；线协议只有 1..65535。
  // 65535 后映射回 1，保持 CAN V1 的 RFC-1982 同形比较合同。
  wire.sequence =
      static_cast<std::uint16_t>(((cmd.sequence - 1U) % 0xFFFFU) + 1U);
  wire.values = static_cast<std::uint8_t>(cmd.values & 0xFFu);
  wire.validity_10ms = validity.value();
  return wire;
}

} // namespace

CanIoLoop::CanIoLoop(CanIoConfig config, LinuxRuntime &runtime,
                     BoundedInputQueue &queue, EventFd &stop_event,
                     SignalFd &signals)
    : config_(std::move(config)), runtime_(runtime), queue_(queue),
      stop_event_(stop_event), signals_(signals), bus_(config_.can_if) {}

CanIoLoop::~CanIoLoop() {
  request_stop();
  join();
}

bool CanIoLoop::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

IoStopReason CanIoLoop::stop_reason() const noexcept {
  return stop_reason_.load(std::memory_order_acquire);
}

CanIoStats CanIoLoop::stats() const {
  CanIoStats out{};
  out.frames_received = frames_received_.load(std::memory_order_relaxed);
  out.frames_sent = frames_sent_.load(std::memory_order_relaxed);
  out.decode_rejects = decode_rejects_.load(std::memory_order_relaxed);
  out.queue_rejects = queue_rejects_.load(std::memory_order_relaxed);
  out.send_would_block_retries =
      send_would_block_retries_.load(std::memory_order_relaxed);
  out.wakeups = wakeups_.load(std::memory_order_relaxed);
  out.stop_reason = stop_reason();
  out.last_errno = last_errno_.load(std::memory_order_relaxed);
  return out;
}

Result<void> CanIoLoop::maybe_set_affinity() {
  if (config_.cpu_affinity < 0) {
    return Result<void>::success();
  }
  if (config_.cpu_affinity >= CPU_SETSIZE) {
    return Error{Errc::InvalidArgument,
                 "I/O CPU affinity is outside CPU_SETSIZE"};
  }
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(static_cast<unsigned>(config_.cpu_affinity), &set);
  const int rc = ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
  if (rc != 0) {
    // pthread_* 直接返回错误号，不保证写 errno。保留 rc
    // 才能正确分类权限/参数失败。
    const Errc code =
        rc == EINVAL
            ? Errc::InvalidArgument
            : ((rc == EPERM || rc == EACCES) ? Errc::Rejected : Errc::IoError);
    last_errno_.store(rc, std::memory_order_relaxed);
    return Error{code, std::string("pthread_setaffinity_np(io): ") +
                           std::strerror(rc)};
  }
  return Result<void>::success();
}

Result<void> CanIoLoop::setup_locked() {
  if (!reactor_.valid()) {
    return Error{Errc::IoError, "epoll reactor invalid"};
  }
  if (!stop_event_.valid() || !signals_.valid()) {
    return Error{Errc::NotOpen, "stop eventfd or signalfd not open"};
  }

  auto opened = bus_.open();
  if (!opened) {
    return opened.error();
  }
  auto nonblock = bus_.set_nonblocking(true);
  if (!nonblock) {
    bus_.close();
    return nonblock.error();
  }

  // 注册顺序不重要；每次唤醒都优先检查停止 fd，避免 CAN 洪泛饿死退出。
  if (auto rc =
          reactor_.add(bus_.native_handle(), EPOLLIN | EPOLLERR | EPOLLHUP);
      !rc) {
    bus_.close();
    return rc.error();
  }
  if (auto rc = reactor_.add(stop_event_.native_handle(), EPOLLIN); !rc) {
    (void)reactor_.remove(bus_.native_handle());
    bus_.close();
    return rc.error();
  }
  if (auto rc = reactor_.add(signals_.native_handle(), EPOLLIN); !rc) {
    (void)reactor_.remove(stop_event_.native_handle());
    (void)reactor_.remove(bus_.native_handle());
    bus_.close();
    return rc.error();
  }
  return Result<void>::success();
}

void CanIoLoop::teardown_fds() {
  // 先从 epoll 删除，再关闭业务 fd，避免兴趣列表指向已关闭整数。
  if (bus_.is_open()) {
    (void)reactor_.remove(bus_.native_handle());
  }
  if (stop_event_.valid()) {
    (void)reactor_.remove(stop_event_.native_handle());
  }
  if (signals_.valid()) {
    (void)reactor_.remove(signals_.native_handle());
  }
  bus_.close();
}

Result<void> CanIoLoop::start() {
  if (thread_.joinable() || running_.load(std::memory_order_acquire)) {
    return Error{Errc::Busy, "CanIoLoop already started"};
  }
  stop_requested_.store(false, std::memory_order_release);
  stop_reason_.store(IoStopReason::None, std::memory_order_release);
  startup_done_.store(false, std::memory_order_release);
  startup_error_ = Error{};

  auto setup = setup_locked();
  if (!setup) {
    return setup.error();
  }

  try {
    thread_ = std::thread([this] { thread_main(); });
  } catch (...) {
    teardown_fds();
    return Error{Errc::IoError, "failed to create I/O thread"};
  }

  // 等待线程进入 loop 前的 running 发布，避免 wait_and_stop
  // 在启动瞬间误判已退出。
  for (int i = 0; i < 1000; ++i) {
    if (startup_done_.load(std::memory_order_acquire)) {
      if (!running_.load(std::memory_order_acquire)) {
        join();
        return startup_error_
                   ? startup_error_
                   : Error{Errc::IoError, "I/O thread failed during startup"};
      }
      return Result<void>::success();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  request_stop();
  join();
  return Error{Errc::Timeout, "I/O thread startup handshake timed out"};
}

void CanIoLoop::request_stop() {
  stop_requested_.store(true, std::memory_order_release);
  // 若线程尚未处理事件，保留已有更具体 reason；否则标记为内部 eventfd 停止。
  IoStopReason expected = IoStopReason::None;
  stop_reason_.compare_exchange_strong(expected, IoStopReason::EventFd,
                                       std::memory_order_acq_rel);
  if (stop_event_.valid()) {
    (void)stop_event_.signal_stop();
  }
}

void CanIoLoop::join() {
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false, std::memory_order_release);
}

bool CanIoLoop::push_event(RuntimeInputEvent event) {
  if (!queue_.try_push(event)) {
    queue_rejects_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void CanIoLoop::handle_stop_event() {
  (void)stop_event_.drain();
  stop_reason_.store(IoStopReason::EventFd, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
}

void CanIoLoop::handle_signal() {
  (void)signals_.drain();
  stop_reason_.store(IoStopReason::Signal, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
}

void CanIoLoop::handle_can_ready() {
  std::size_t budget = config_.max_frames_per_wake;
  while (budget > 0 && !stop_requested_.load(std::memory_order_acquire)) {
    // timeout<0：明确允许阻塞 read；此处 fd 已非阻塞，EAGAIN 即排空结束。
    auto frame = bus_.receive(std::chrono::milliseconds{-1});
    if (!frame) {
      if (frame.error().code() == Errc::WouldBlock) {
        return;
      }
      last_errno_.store(errno, std::memory_order_relaxed);
      RuntimeInputEvent event{};
      event.kind = RuntimeInputKind::IoError;
      event.node_id = config_.node_id;
      const auto now = monotonic_now_ns();
      event.monotonic_ns = now ? now.value() : 0;
      (void)push_event(event);
      stop_reason_.store(IoStopReason::IoError, std::memory_order_release);
      stop_requested_.store(true, std::memory_order_release);
      return;
    }

    frames_received_.fetch_add(1, std::memory_order_relaxed);
    --budget;

    const auto now = monotonic_now_ns();
    const std::int64_t now_ns = now ? now.value() : 0;
    auto decoded = can_v1::decode(frame.value());
    if (!decoded) {
      decode_rejects_.fetch_add(1, std::memory_order_relaxed);
      RuntimeInputEvent event{};
      event.kind = RuntimeInputKind::ProtocolReject;
      event.node_id = config_.node_id;
      event.monotonic_ns = now_ns;
      (void)push_event(event);
      continue;
    }

    RuntimeInputEvent event{};
    event.monotonic_ns = now_ns;
    event.node_id = config_.node_id;
    switch (decoded.value().kind) {
    case can_v1::MessageKind::Heartbeat:
      event.kind = RuntimeInputKind::Heartbeat;
      event.node_id = decoded.value().heartbeat.node_id;
      event.boot_id = decoded.value().heartbeat.boot_id;
      event.session_id = decoded.value().heartbeat.session_id;
      event.hb_seq = decoded.value().heartbeat.hb_seq;
      break;
    case can_v1::MessageKind::Status:
      event.kind = RuntimeInputKind::NodeStatus;
      event.node_id = decoded.value().status.node_id;
      event.session_id = decoded.value().status.session_id;
      event.interlock_ready = decoded.value().status.interlock_ready;
      event.input_bits = decoded.value().status.input_bits;
      event.node_fault_code = decoded.value().status.fault_code;
      break;
    case can_v1::MessageKind::OutputStatus:
      event.kind = RuntimeInputKind::OutputStatus;
      event.node_id = decoded.value().output_status.node_id;
      event.session_id = decoded.value().output_status.session_id;
      event.output_result = decoded.value().output_status.result;
      event.output_sequence = decoded.value().output_status.sequence;
      event.output_mirror = decoded.value().output_status.output_mirror;
      break;
    case can_v1::MessageKind::OutputCommand:
      // Runtime 不消费自己的下行命令回环；忽略。
      continue;
    }
    (void)push_event(event);
  }
}

void CanIoLoop::pump_output() {
  // mailbox 中若有更新目标，它覆盖尚未成功发送的旧 pending，维持 latest-wins。
  if (auto latest = runtime_.try_consume_output_command(); latest.has_value()) {
    pending_output_ = *latest;
  }
  if (!pending_output_.has_value()) {
    return;
  }
  if (!runtime_.output_command_sendable(*pending_output_)) {
    pending_output_.reset();
    return;
  }
  const auto now = monotonic_now_ns();
  if (!now) {
    pending_output_.reset();
    return;
  }
  auto wire = to_wire_command(*pending_output_, config_.node_id, now.value());
  if (!wire) {
    // 编码失败视为应用层合同问题：注入故障事件，不把坏帧送上总线。
    RuntimeInputEvent event{};
    event.kind = RuntimeInputKind::ProtocolReject;
    event.node_id = config_.node_id;
    event.monotonic_ns = now.value();
    (void)push_event(event);
    pending_output_.reset();
    return;
  }
  auto encoded = can_v1::encode_output_command(wire.value());
  if (!encoded) {
    RuntimeInputEvent event{};
    event.kind = RuntimeInputKind::ProtocolReject;
    event.node_id = config_.node_id;
    event.monotonic_ns = now.value();
    (void)push_event(event);
    pending_output_.reset();
    return;
  }
  auto sent = bus_.send(encoded.value());
  if (!sent) {
    if (sent.error().code() == Errc::WouldBlock) {
      // 保留 pending；下次泵出前会重新检查状态、session 与 deadline。
      send_would_block_retries_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    last_errno_.store(errno, std::memory_order_relaxed);
    RuntimeInputEvent event{};
    event.kind = RuntimeInputKind::IoError;
    event.node_id = config_.node_id;
    event.monotonic_ns = now.value();
    (void)push_event(event);
    stop_reason_.store(IoStopReason::SendFailure, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
    pending_output_.reset();
    return;
  }
  frames_sent_.fetch_add(1, std::memory_order_relaxed);
  const auto tracked = runtime_.note_output_command_sent(
      wire.value().session_id, wire.value().sequence, now.value());
  if (!tracked) {
    // 帧已经进入内核发送路径却无法登记 ACK 监督，继续运行会把执行闭环降级回
    // best-effort。注入内部 I/O 故障并停线程，交给 Runtime 原子关闭输出。
    RuntimeInputEvent event{};
    event.kind = RuntimeInputKind::IoError;
    event.node_id = config_.node_id;
    event.monotonic_ns = now.value();
    (void)push_event(event);
    stop_reason_.store(IoStopReason::SendFailure, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
  }
  pending_output_.reset();
}

void CanIoLoop::thread_main() {
  if (auto aff = maybe_set_affinity(); !aff) {
    startup_error_ = aff.error();
    stop_reason_.store(IoStopReason::IoError, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    startup_done_.store(true, std::memory_order_release);
    teardown_fds();
    return;
  }

  // 先进入可运行态再发布 startup_done，保证 start() 返回后 wait_and_stop
  // 不会误判。
  running_.store(true, std::memory_order_release);
  startup_done_.store(true, std::memory_order_release);

  while (!stop_requested_.load(std::memory_order_acquire)) {
    // 短超时让输出泵在无 CAN 流量时仍能推进；停止仍由 eventfd/signalfd
    // 立即唤醒。
    auto ready = reactor_.wait(std::chrono::milliseconds{10}, 16);
    if (!ready) {
      last_errno_.store(errno, std::memory_order_relaxed);
      stop_reason_.store(IoStopReason::IoError, std::memory_order_release);
      break;
    }
    wakeups_.fetch_add(1, std::memory_order_relaxed);

    bool saw_stop = false;
    bool saw_signal = false;
    bool saw_can = false;
    bool can_error = false;
    for (const auto &item : ready.value()) {
      if (item.fd == stop_event_.native_handle()) {
        saw_stop = true;
      } else if (item.fd == signals_.native_handle()) {
        saw_signal = true;
      } else if (item.fd == bus_.native_handle()) {
        saw_can = true;
        if ((item.events & (EPOLLERR | EPOLLHUP)) != 0U) {
          can_error = true;
        }
      }
    }

    // 停止事件优先于 CAN 洪泛。
    if (saw_stop) {
      handle_stop_event();
    }
    if (saw_signal) {
      handle_signal();
    }
    if (can_error) {
      RuntimeInputEvent event{};
      event.kind = RuntimeInputKind::IoError;
      event.node_id = config_.node_id;
      const auto now = monotonic_now_ns();
      event.monotonic_ns = now ? now.value() : 0;
      (void)push_event(event);
      stop_reason_.store(IoStopReason::IoError, std::memory_order_release);
      stop_requested_.store(true, std::memory_order_release);
    } else if (saw_can && !stop_requested_.load(std::memory_order_acquire)) {
      handle_can_ready();
    }

    if (!stop_requested_.load(std::memory_order_acquire)) {
      pump_output();
    }
  }

  teardown_fds();
  pending_output_.reset();
  running_.store(false, std::memory_order_release);
}

} // namespace rcr
