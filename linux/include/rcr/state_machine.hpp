#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"
#include "rcr/types.hpp"

#include <optional>
#include <string>

namespace rcr {

struct TransitionResult {
  bool accepted{false};
  RuntimeMode from{RuntimeMode::Disabled};
  RuntimeMode to{RuntimeMode::Disabled};
  std::string reason{};
};

/**
 * Runtime Core 内的纯软件状态机：不依赖 ROS2、SocketCAN，也不自行创建线程。
 *
 * 只有 mode == Active 且 interlock_ready 为真时才接受普通输出命令。
 * interlock_ready 是演示系统的软件前置条件，不是安全认证信号；状态机中的
 * EStop 同样只用于学习锁存和恢复逻辑，不能替代真实机器的硬件急停回路。
 */
class RuntimeStateMachine {
 public:
  RuntimeStateMachine();

  [[nodiscard]] RuntimeMode mode() const noexcept;
  [[nodiscard]] bool interlock_ready() const noexcept;
  [[nodiscard]] FaultCode fault() const noexcept;
  [[nodiscard]] bool can_accept_output() const noexcept;

  /// 更新节点/模拟器上报的软件联锁；Active 中丢失联锁会立即转入 Hold。
  void set_interlock_ready(bool ready);
  void set_fault(FaultCode code);

  [[nodiscard]] TransitionResult handle(RuntimeEvent event);

 private:
  [[nodiscard]] TransitionResult reject(RuntimeEvent event, std::string reason) const;
  [[nodiscard]] TransitionResult accept(RuntimeMode next, RuntimeEvent event, std::string reason);

  RuntimeMode mode_{RuntimeMode::Disabled};
  bool interlock_ready_{false};
  FaultCode fault_{FaultCode::None};
};

}  // namespace rcr
