// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/state_machine.hpp"

namespace rcr {

RuntimeStateMachine::RuntimeStateMachine() = default;

RuntimeMode RuntimeStateMachine::mode() const noexcept { return mode_; }

bool RuntimeStateMachine::interlock_ready() const noexcept { return interlock_ready_; }

FaultCode RuntimeStateMachine::fault() const noexcept { return fault_; }

bool RuntimeStateMachine::can_accept_output() const noexcept {
  return mode_ == RuntimeMode::Active && interlock_ready_;
}

void RuntimeStateMachine::set_interlock_ready(bool ready) {
  // 统一走事件入口，保证直接 handle 联锁事件与 setter 不会产生两套状态语义。
  (void)handle(ready ? RuntimeEvent::InterlockReady : RuntimeEvent::InterlockLost);
}

void RuntimeStateMachine::set_fault(FaultCode code) { fault_ = code; }

TransitionResult RuntimeStateMachine::reject(RuntimeEvent event, std::string reason) const {
  TransitionResult result;
  result.accepted = false;
  result.from = mode_;
  result.to = mode_;
  result.reason = std::move(reason);
  result.reason.append(" (event=");
  result.reason.append(to_string(event));
  result.reason.push_back(')');
  return result;
}

TransitionResult RuntimeStateMachine::accept(RuntimeMode next, RuntimeEvent event,
                                             std::string reason) {
  TransitionResult result;
  result.accepted = true;
  result.from = mode_;
  result.to = next;
  result.reason = std::move(reason);
  result.reason.append(" (event=");
  result.reason.append(to_string(event));
  result.reason.push_back(')');
  mode_ = next;
  return result;
}

TransitionResult RuntimeStateMachine::handle(RuntimeEvent event) {
  // 联锁值和迁移在同一次事件处理中更新，避免“状态已 Hold、联锁值仍为真”的矛盾快照。
  if (event == RuntimeEvent::InterlockReady) {
    interlock_ready_ = true;
  } else if (event == RuntimeEvent::InterlockLost) {
    interlock_ready_ = false;
  }

  // 急停事件具有全局最高优先级；进入 EStop 后只能走受许可约束的复位路径。
  if (event == RuntimeEvent::EStopTrigger) {
    fault_ = FaultCode::None;
    return accept(RuntimeMode::EStop, event, "emergency stop latched");
  }

  switch (mode_) {
    case RuntimeMode::Disabled:
      if (event == RuntimeEvent::Boot) {
        return accept(RuntimeMode::Idle, event, "boot complete");
      }
      if (event == RuntimeEvent::DeactivateRequest) {
        return accept(RuntimeMode::Disabled, event, "already disabled");
      }
      if (event == RuntimeEvent::ActivateRequest) {
        return reject(event, "must boot to idle before activation");
      }
      if (event == RuntimeEvent::EStopReset) {
        return reject(event, "not in ESTOP");
      }
      break;

    case RuntimeMode::Idle:
      if (event == RuntimeEvent::ActivateRequest) {
        if (!interlock_ready_) {
          return reject(event, "interlock must be ready before activation");
        }
        fault_ = FaultCode::None;
        return accept(RuntimeMode::Active, event, "output path activated");
      }
      if (event == RuntimeEvent::DeactivateRequest) {
        return accept(RuntimeMode::Disabled, event, "disabled from idle");
      }
      if (event == RuntimeEvent::FaultDetected) {
        return accept(RuntimeMode::Fault, event, "fault while idle");
      }
      break;

    case RuntimeMode::Active:
      if (event == RuntimeEvent::DeactivateRequest) {
        return accept(RuntimeMode::Idle, event, "deactivated");
      }
      if (event == RuntimeEvent::CommandTimeout) {
        fault_ = FaultCode::Watchdog;
        return accept(RuntimeMode::Hold, event, "command timeout hold");
      }
      if (event == RuntimeEvent::InterlockLost) {
        fault_ = FaultCode::InterlockLost;
        return accept(RuntimeMode::Hold, event, "software interlock lost");
      }
      if (event == RuntimeEvent::FaultDetected) {
        return accept(RuntimeMode::Fault, event, "fault while active");
      }
      if (event == RuntimeEvent::Hold) {
        return accept(RuntimeMode::Hold, event, "explicit hold");
      }
      break;

    case RuntimeMode::Hold:
      if (event == RuntimeEvent::Resume) {
        if (!interlock_ready_) {
          return reject(event, "cannot acknowledge hold without interlock");
        }
        if (fault_ == FaultCode::Watchdog || fault_ == FaultCode::InterlockLost ||
            fault_ == FaultCode::None) {
          fault_ = FaultCode::None;
          // 恢复只回到 Idle；必须重新 Activate，避免旧命令导致输出自动恢复。
          return accept(RuntimeMode::Idle, event, "hold acknowledged; activation required");
        }
        return reject(event, "clear fault before resume");
      }
      if (event == RuntimeEvent::DeactivateRequest) {
        fault_ = FaultCode::None;
        return accept(RuntimeMode::Disabled, event, "disabled from hold");
      }
      if (event == RuntimeEvent::FaultDetected) {
        return accept(RuntimeMode::Fault, event, "fault while holding");
      }
      if (event == RuntimeEvent::InterlockReady) {
        // 联锁恢复不等于恢复输出，必须等待显式 Resume 和新的 Activate。
        return accept(RuntimeMode::Hold, event, "interlock restored; still holding");
      }
      break;

    case RuntimeMode::Fault:
      if (event == RuntimeEvent::FaultCleared) {
        fault_ = FaultCode::None;
        return accept(RuntimeMode::Idle, event, "fault cleared to idle");
      }
      if (event == RuntimeEvent::DeactivateRequest) {
        fault_ = FaultCode::None;
        return accept(RuntimeMode::Disabled, event, "disabled from fault");
      }
      break;

    case RuntimeMode::EStop:
      if (event == RuntimeEvent::EStopReset) {
        if (!interlock_ready_) {
          return reject(event, "cannot reset ESTOP while interlock is open");
        }
        fault_ = FaultCode::None;
        return accept(RuntimeMode::Idle, event, "estop reset to idle");
      }
      if (event == RuntimeEvent::ActivateRequest || event == RuntimeEvent::Resume) {
        return reject(event, "ESTOP latched; reset required");
      }
      break;
  }

  // 其他模式下联锁事件只更新信息，不隐式激活输出。
  if (event == RuntimeEvent::InterlockReady || event == RuntimeEvent::InterlockLost) {
    return accept(mode_, event, "interlock information updated");
  }

  return reject(event, "transition not allowed in current mode");
}

}  // namespace rcr
