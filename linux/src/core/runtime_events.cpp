// Runtime Core：有界输入队列与单节点监督；不负责 SocketCAN 收发。
#include "rcr/runtime_events.hpp"

#include "rcr/runtime.hpp"

namespace rcr {

BoundedInputQueue::BoundedInputQueue(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {
  // 容量在构造时固定分配，周期路径不再扩容，避免监督线程隐式分配。
  storage_.resize(capacity_);
}

std::size_t BoundedInputQueue::size() const {
  std::lock_guard lock(mutex_);
  return size_;
}

bool BoundedInputQueue::empty() const {
  std::lock_guard lock(mutex_);
  return size_ == 0;
}

std::uint64_t BoundedInputQueue::push_count() const noexcept {
  return push_count_.load(std::memory_order_relaxed);
}

std::uint64_t BoundedInputQueue::drop_count() const noexcept {
  return drop_count_.load(std::memory_order_relaxed);
}

std::uint64_t BoundedInputQueue::overflow_count() const noexcept {
  return overflow_count_.load(std::memory_order_relaxed);
}

bool BoundedInputQueue::overflow_latched() const noexcept {
  return overflow_latched_.load(std::memory_order_acquire);
}

bool BoundedInputQueue::try_push(const RuntimeInputEvent& event) {
  std::lock_guard lock(mutex_);
  if (size_ >= capacity_) {
    // 满队列时不覆盖任何已有事件：故障边沿必须可见，宁可锁存 overflow。
    drop_count_.fetch_add(1, std::memory_order_relaxed);
    overflow_count_.fetch_add(1, std::memory_order_relaxed);
    overflow_latched_.store(true, std::memory_order_release);
    return false;
  }
  const std::size_t index = (head_ + size_) % capacity_;
  storage_[index] = event;
  ++size_;
  push_count_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

std::optional<RuntimeInputEvent> BoundedInputQueue::try_pop() {
  std::lock_guard lock(mutex_);
  if (size_ == 0) {
    return std::nullopt;
  }
  RuntimeInputEvent event = storage_[head_];
  head_ = (head_ + 1) % capacity_;
  --size_;
  return event;
}

void BoundedInputQueue::clear_overflow_latch() noexcept {
  overflow_latched_.store(false, std::memory_order_release);
}

NodeSupervisor::NodeSupervisor(NodeSupervisorConfig config, BoundedInputQueue& queue)
    : config_(config), queue_(queue) {}

void NodeSupervisor::clear_restart_latch() noexcept {
  std::lock_guard lock(mutex_);
  restart_latched_ = false;
}

NodeSupervisorSnapshot NodeSupervisor::snapshot() const {
  std::lock_guard lock(mutex_);
  NodeSupervisorSnapshot out{};
  out.ever_seen = ever_seen_;
  out.online = online_;
  out.restart_latched = restart_latched_;
  out.overflow_fault_latched = overflow_fault_latched_;
  out.comm_loss_latched = comm_loss_latched_;
  out.boot_id = boot_id_;
  out.session_id = session_id_;
  out.last_hb_seq = last_hb_seq_;
  out.node_fault_code = node_fault_code_;
  out.last_heartbeat_ns = last_heartbeat_ns_;
  out.heartbeats = heartbeats_;
  out.status_updates = status_updates_;
  out.protocol_rejects = protocol_rejects_;
  out.events_processed = events_processed_;
  out.events_budget_left = events_budget_left_;
  return out;
}

Result<void> NodeSupervisor::acknowledge_fault_clear(FaultCode fault) {
  std::lock_guard lock(mutex_);
  switch (fault) {
    case FaultCode::CommLoss:
      if (!online_ || comm_loss_latched_) {
        return Error{Errc::Rejected, "heartbeat has not recovered"};
      }
      return Result<void>::success();
    case FaultCode::NodeFault:
      if (!online_) {
        return Error{Errc::Rejected, "node is not online"};
      }
      if (node_fault_code_ != 0) {
        return Error{Errc::Rejected, "node still reports a fault"};
      }
      // 用户的 clear 动作同时确认已经观察到节点新 boot/session；仍只回 Idle。
      restart_latched_ = false;
      return Result<void>::success();
    case FaultCode::Internal:
      if (overflow_fault_latched_ || queue_.overflow_latched()) {
        return Error{Errc::Rejected, "event queue overflow requires daemon restart"};
      }
      return Result<void>::success();
    case FaultCode::None:
    case FaultCode::Watchdog:
    case FaultCode::InterlockLost:
    case FaultCode::InputFault:
    case FaultCode::ProtocolReject:
      // 这些故障没有 NodeSupervisor 内部锁存；状态机自身的 clear 条件仍会检查联锁等约束。
      return Result<void>::success();
  }
  return Error{Errc::Rejected, "unknown fault recovery state"};
}

void NodeSupervisor::on_tick(LinuxRuntime& runtime, std::int64_t now_ns) {
  // 先处理 overflow 锁存：队列可能已空，但溢出事实仍必须 fail-closed。
  if (queue_.overflow_latched()) {
    apply_overflow_fault(runtime);
  }

  std::size_t budget = config_.max_events_per_tick;
  while (budget > 0) {
    auto event = queue_.try_pop();
    if (!event.has_value()) {
      break;
    }
    apply_event(runtime, *event, now_ns);
    --budget;
  }

  {
    std::lock_guard lock(mutex_);
    events_budget_left_ = budget;
  }

  apply_comm_loss(runtime, now_ns);
}

void NodeSupervisor::apply_overflow_fault(LinuxRuntime& runtime) {
  bool need_fault = false;
  {
    std::lock_guard lock(mutex_);
    if (!overflow_fault_latched_) {
      overflow_fault_latched_ = true;
      need_fault = true;
    }
  }
  if (!need_fault) {
    return;
  }
  runtime.set_fault(FaultCode::Internal);
  (void)runtime.handle(RuntimeEvent::FaultDetected);
}

void NodeSupervisor::apply_comm_loss(LinuxRuntime& runtime, std::int64_t now_ns) {
  bool need_fault = false;
  {
    std::lock_guard lock(mutex_);
    if (!ever_seen_ || !online_) {
      return;
    }
    if (config_.heartbeat_timeout.count() <= 0) {
      return;
    }
    if (now_ns - last_heartbeat_ns_ < config_.heartbeat_timeout.count()) {
      return;
    }
    online_ = false;
    if (!comm_loss_latched_) {
      comm_loss_latched_ = true;
      need_fault = true;
    }
  }
  if (need_fault) {
    runtime.set_fault(FaultCode::CommLoss);
    (void)runtime.handle(RuntimeEvent::FaultDetected);
  }
}

void NodeSupervisor::note_node_restart(LinuxRuntime& runtime, std::uint16_t boot_id,
                                       std::uint16_t session_id) {
  {
    std::lock_guard lock(mutex_);
    boot_id_ = boot_id;
    session_id_ = session_id;
    restart_latched_ = true;
    online_ = true;
    comm_loss_latched_ = false;
  }
  // 节点重启清除旧会话理解：离开 Active 进入 Fault，禁止自动重放。
  runtime.set_fault(FaultCode::NodeFault);
  (void)runtime.handle(RuntimeEvent::FaultDetected);
}

void NodeSupervisor::apply_event(LinuxRuntime& runtime, const RuntimeInputEvent& event,
                                 std::int64_t now_ns) {
  {
    std::lock_guard lock(mutex_);
    ++events_processed_;
  }

  if (event.node_id != 0 && event.node_id != config_.node_id) {
    return;
  }

  switch (event.kind) {
    case RuntimeInputKind::Heartbeat: {
      bool restart = false;
      {
        std::lock_guard lock(mutex_);
        ++heartbeats_;
        if (!ever_seen_) {
          ever_seen_ = true;
          boot_id_ = event.boot_id;
          session_id_ = event.session_id;
        } else if (event.boot_id != boot_id_ || event.session_id != session_id_) {
          restart = true;
        }
        if (!restart) {
          online_ = true;
          boot_id_ = event.boot_id;
          session_id_ = event.session_id;
          last_hb_seq_ = event.hb_seq;
          last_heartbeat_ns_ = event.monotonic_ns != 0 ? event.monotonic_ns : now_ns;
          comm_loss_latched_ = false;
        }
      }
      if (restart) {
        note_node_restart(runtime, event.boot_id, event.session_id);
        std::lock_guard lock(mutex_);
        last_heartbeat_ns_ = event.monotonic_ns != 0 ? event.monotonic_ns : now_ns;
        last_hb_seq_ = event.hb_seq;
      }
      break;
    }
    case RuntimeInputKind::NodeStatus: {
      {
        std::lock_guard lock(mutex_);
        ++status_updates_;
        node_fault_code_ = event.node_fault_code;
      }
      bool session_mismatch = false;
      std::uint16_t known_boot = 0;
      {
        std::lock_guard lock(mutex_);
        known_boot = boot_id_;
        if (ever_seen_ && event.session_id != 0 && event.session_id != session_id_) {
          session_mismatch = true;
        }
      }
      if (session_mismatch) {
        note_node_restart(runtime, known_boot, event.session_id);
      }
      runtime.set_interlock_ready(event.interlock_ready);
      if (event.node_fault_code != 0) {
        runtime.set_fault(FaultCode::NodeFault);
        (void)runtime.handle(RuntimeEvent::FaultDetected);
      }
      break;
    }
    case RuntimeInputKind::OutputStatus:
      break;
    case RuntimeInputKind::ProtocolReject: {
      {
        std::lock_guard lock(mutex_);
        ++protocol_rejects_;
      }
      // 单帧坏数据只计数；持续拒绝可由上层策略升级，V1 不因单帧退出。
      break;
    }
    case RuntimeInputKind::IoError:
      runtime.set_fault(FaultCode::Internal);
      (void)runtime.handle(RuntimeEvent::FaultDetected);
      break;
  }
}

}  // namespace rcr
