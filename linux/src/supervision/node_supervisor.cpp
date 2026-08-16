// Device Supervision：解释单节点事件并驱动 Runtime 策略；不负责 SocketCAN
// 收发。
#include "rcr/runtime_events.hpp"

#include "rcr/runtime.hpp"

namespace rcr {

NodeSupervisor::NodeSupervisor(NodeSupervisorConfig config,
                               BoundedInputQueue &queue)
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
  out.input_bits = input_bits_;
  out.last_output_mirror = last_output_mirror_;
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
  // FaultCode 会被后续故障覆盖，所以先检查全部持久条件；不能用 switch(fault) 把它误当
  // active fault set。overflow 表示输入完整性已经丢失，即使之后 CommLoss 恢复也必须重启。
  if (overflow_fault_latched_ || queue_.overflow_latched()) {
    return Error{Errc::Rejected,
                 "event queue overflow requires daemon restart"};
  }
  if (comm_loss_latched_) {
    return Error{Errc::Rejected, "heartbeat has not recovered"};
  }

  // 一旦本进程观察过 heartbeat/status，离线本身就是恢复 blocker。status 可能先于首个
  // heartbeat 到达，因此不能只看 ever_seen_。
  if ((ever_seen_ || status_updates_ != 0 || restart_latched_) && !online_) {
    return Error{Errc::Rejected, "node is not online"};
  }
  if (node_fault_code_ != 0) {
    return Error{Errc::Rejected, "node still reports a fault"};
  }

  switch (fault) {
    case FaultCode::None:
    case FaultCode::Watchdog:
    case FaultCode::InterlockLost:
    case FaultCode::InputFault:
    case FaultCode::ProtocolReject:
    case FaultCode::CommLoss:
    case FaultCode::NodeFault:
    case FaultCode::Internal:
    case FaultCode::AckTimeout:
      break;
    default:
      return Error{Errc::Rejected, "unknown fault recovery state"};
  }

  // 用户的 clear 动作确认已经观察到节点新 boot/session；清的只是确认锁存，状态机随后
  // 仍只能 Fault → Idle，旧会话命令不会自动重放。
  restart_latched_ = false;
  return Result<void>::success();
}

void NodeSupervisor::on_tick(LinuxRuntime &runtime, std::int64_t now_ns) {
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

void NodeSupervisor::apply_overflow_fault(LinuxRuntime &runtime) {
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
  (void)runtime.raise_fault(FaultCode::Internal);
}

void NodeSupervisor::apply_comm_loss(LinuxRuntime &runtime,
                                     std::int64_t now_ns) {
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
    (void)runtime.raise_fault(FaultCode::CommLoss);
  }
}

void NodeSupervisor::note_node_restart(LinuxRuntime &runtime,
                                       std::uint16_t boot_id,
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
  (void)runtime.raise_fault(FaultCode::NodeFault);
}

void NodeSupervisor::apply_event(LinuxRuntime &runtime,
                                 const RuntimeInputEvent &event,
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
        last_heartbeat_ns_ =
            event.monotonic_ns != 0 ? event.monotonic_ns : now_ns;
        comm_loss_latched_ = false;
      }
    }
    if (restart) {
      note_node_restart(runtime, event.boot_id, event.session_id);
      std::lock_guard lock(mutex_);
      last_heartbeat_ns_ =
          event.monotonic_ns != 0 ? event.monotonic_ns : now_ns;
      last_hb_seq_ = event.hb_seq;
    }
    break;
  }
  case RuntimeInputKind::NodeStatus: {
    {
      std::lock_guard lock(mutex_);
      ++status_updates_;
      node_fault_code_ = event.node_fault_code;
      // 保留最近数字输入快照供 UI/CellReady；bit0 到位不是 Fault 来源。
      input_bits_ = event.input_bits;
    }
    bool session_mismatch = false;
    std::uint16_t known_boot = 0;
    {
      std::lock_guard lock(mutex_);
      known_boot = boot_id_;
      if (ever_seen_ && event.session_id != 0 &&
          event.session_id != session_id_) {
        session_mismatch = true;
      }
    }
    if (session_mismatch) {
      note_node_restart(runtime, known_boot, event.session_id);
    }
    runtime.set_interlock_ready(event.interlock_ready);
    if (event.node_fault_code != 0) {
      (void)runtime.raise_fault(FaultCode::NodeFault);
    }
    break;
  }
  case RuntimeInputKind::OutputStatus:
    {
      std::lock_guard lock(mutex_);
      last_output_mirror_ = event.output_mirror;
    }
    runtime.observe_output_status(
        event.session_id, event.output_sequence, event.output_result,
        event.monotonic_ns != 0 ? event.monotonic_ns : now_ns);
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
    (void)runtime.raise_fault(FaultCode::Internal);
    break;
  }
}

} // namespace rcr
