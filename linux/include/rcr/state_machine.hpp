#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/result.hpp"
#include "rcr/types.hpp"

#include <optional>
#include <string>

namespace rcr {

struct TransitionResult {
  /// false 表示事件被拒绝，状态保持不变；拒绝本身也是正常、可诊断的业务结果。
  bool accepted{false};
  /// from/to 让调用方无需再次读取状态即可记录一次完整迁移。
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
 *
 * 本类故意不带 mutex/atomic：它表达确定性的单线程状态规则，线程安全由组合根
 * LinuxRuntime 的 state_mutex
 * 提供。这样单测可以直接驱动事件，也避免状态机内部锁 与 Runtime 的
 * mailbox/watchdog 锁形成隐藏顺序。
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
  /// 纯状态规则测试可分开设置 code；并发 Runtime 调用方必须使用
  /// LinuxRuntime::raise_fault。
  void set_fault(FaultCode code);

  /// 处理一个事件并返回接受/拒绝原因；无论结果如何都不抛异常表达业务拒绝。
  [[nodiscard]] TransitionResult handle(RuntimeEvent event);

private:
  [[nodiscard]] TransitionResult reject(RuntimeEvent event,
                                        std::string reason) const;
  [[nodiscard]] TransitionResult accept(RuntimeMode next, RuntimeEvent event,
                                        std::string reason);

  RuntimeMode mode_{RuntimeMode::Disabled};
  bool interlock_ready_{false};
  FaultCode fault_{FaultCode::None};
};

} // namespace rcr
