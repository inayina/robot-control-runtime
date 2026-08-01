// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/state_machine.hpp"

namespace rcr {

RuntimeStateMachine::RuntimeStateMachine() = default;

RuntimeMode RuntimeStateMachine::mode() const noexcept { return mode_; }

bool RuntimeStateMachine::interlock_ready() const noexcept { return interlock_ready_; }

FaultCode RuntimeStateMachine::fault() const noexcept { return fault_; }

bool RuntimeStateMachine::can_accept_output() const noexcept {
  // fault_ 不是单独门控条件：出现需阻止输出的 fault 时，状态迁移必须离开 Active。
  // 这样“状态”和“输出许可”只有一个权威来源，避免 fault 与 mode 各自漂移。
  return mode_ == RuntimeMode::Active && interlock_ready_;
}

void RuntimeStateMachine::set_interlock_ready(bool ready) {
  // 统一走事件入口，保证直接 handle 联锁事件与 setter 不会产生两套状态语义。
  (void)handle(ready ? RuntimeEvent::InterlockReady : RuntimeEvent::InterlockLost);
}

void RuntimeStateMachine::set_fault(FaultCode code) { fault_ = code; }

TransitionResult RuntimeStateMachine::reject(RuntimeEvent event, std::string reason) const {
  // 拒绝不修改 mode/fault/interlock，只把当前状态复制到 from/to。reason 追加事件名，
  // 便于日志在没有额外上下文时仍能回答“哪个请求被拒绝”。
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
  // 先捕获旧 mode 再提交新 mode，确保 TransitionResult 记录真实的迁移前后值。
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

  // 急停事件具有全局最高优先级，不受当前 mode 的 switch 分支限制；进入 EStop 后只能
  // 走受许可约束的复位路径。这里只模拟软件锁存，不表示物理输出已被硬件切断。
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
        // 激活是唯一打开普通输出许可的迁移，必须在迁移点再次验证联锁，不能依赖调用方
        // “之前检查过”的易失条件。
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
        // timeout 进入 Hold 而不是直接 Fault：该类故障允许确认后回 Idle，但绝不直接
        // 恢复 Active；LinuxRuntime 同时清空旧输出路径。
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
        // 清故障只回 Idle，仍需一次新的 Activate，避免清码动作隐式恢复输出。
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
        // 即使收到 Reset，软件联锁未恢复也拒绝；成功复位仍只到 Idle。
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
