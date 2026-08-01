#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include <cstdint>
#include <string_view>

namespace rcr {

/**
 * Runtime Core 的互斥运行模式；任一时刻只有一个 mode。
 * Disabled/Idle/Active/Hold/Fault/EStop 表达软件生命周期，不映射认证安全等级。
 * 类型层不依赖 ROS2，未来 ROS2 只能在 Adapter 层做转换。
 */
enum class RuntimeMode : std::uint8_t {
  Disabled = 0,
  Idle = 1,
  Active = 2,
  Hold = 3,
  Fault = 4,
  EStop = 5,
};

[[nodiscard]] constexpr std::string_view to_string(RuntimeMode mode) noexcept {
  switch (mode) {
    case RuntimeMode::Disabled:
      return "DISABLED";
    case RuntimeMode::Idle:
      return "IDLE";
    case RuntimeMode::Active:
      return "ACTIVE";
    case RuntimeMode::Hold:
      return "HOLD";
    case RuntimeMode::Fault:
      return "FAULT";
    case RuntimeMode::EStop:
      return "ESTOP";
  }
  return "UNKNOWN";
}

/// 驱动 RuntimeStateMachine 状态迁移的离散事件。事件不是可覆盖目标，未来跨线程投递时
/// 必须走有界事件队列，不能放入 latest-wins CommandMailbox。
enum class RuntimeEvent : std::uint8_t {
  Boot = 0,
  ActivateRequest = 1,
  DeactivateRequest = 2,
  CommandTimeout = 3,
  InterlockLost = 4,
  InterlockReady = 5,
  FaultDetected = 6,
  FaultCleared = 7,
  EStopTrigger = 8,
  EStopReset = 9,
  Resume = 10,
  Hold = 11,
};

[[nodiscard]] constexpr std::string_view to_string(RuntimeEvent event) noexcept {
  switch (event) {
    case RuntimeEvent::Boot:
      return "BOOT";
    case RuntimeEvent::ActivateRequest:
      return "ACTIVATE_REQUEST";
    case RuntimeEvent::DeactivateRequest:
      return "DEACTIVATE_REQUEST";
    case RuntimeEvent::CommandTimeout:
      return "COMMAND_TIMEOUT";
    case RuntimeEvent::InterlockLost:
      return "INTERLOCK_LOST";
    case RuntimeEvent::InterlockReady:
      return "INTERLOCK_READY";
    case RuntimeEvent::FaultDetected:
      return "FAULT_DETECTED";
    case RuntimeEvent::FaultCleared:
      return "FAULT_CLEARED";
    case RuntimeEvent::EStopTrigger:
      return "ESTOP_TRIGGER";
    case RuntimeEvent::EStopReset:
      return "ESTOP_RESET";
    case RuntimeEvent::Resume:
      return "RESUME";
    case RuntimeEvent::Hold:
      return "HOLD";
  }
  return "UNKNOWN";
}

/// 当前软件模型可观测的故障原因；FaultCode 与 RuntimeMode 分开，使 Hold 也能说明原因。
enum class FaultCode : std::uint16_t {
  None = 0,
  Watchdog = 1,
  InputFault = 2,
  CommLoss = 3,
  NodeFault = 4,
  ProtocolReject = 5,
  InterlockLost = 6,
  Internal = 7,
};

[[nodiscard]] constexpr std::string_view to_string(FaultCode code) noexcept {
  switch (code) {
    case FaultCode::None:
      return "NONE";
    case FaultCode::Watchdog:
      return "WATCHDOG";
    case FaultCode::InputFault:
      return "INPUT_FAULT";
    case FaultCode::CommLoss:
      return "COMM_LOSS";
    case FaultCode::NodeFault:
      return "NODE_FAULT";
    case FaultCode::ProtocolReject:
      return "PROTOCOL_REJECT";
    case FaultCode::InterlockLost:
      return "INTERLOCK_LOST";
    case FaultCode::Internal:
      return "INTERNAL";
  }
  return "UNKNOWN";
}

/**
 * Runtime 下发给模拟器或可选 ESP32 节点的普通数字输出目标。
 *
 * 该命令表达普通演示 I/O，不具备功能安全语义。mask 中为 1 的位才采用 values
 * 的对应目标值。session_id、sequence 和 deadline_ns 用于拒绝旧会话、乱序及
 * 过期命令；这些软件防护不能替代真实机器的硬件安全回路。
 * 这是进程内类型，不是 CAN wire struct：字段宽度、对齐和绝对 deadline 都与 CAN V1
 * 线级布局不同，codec 必须显式转换，禁止 memcpy。
 */
struct OutputCommand {
  /// 每次进程启动或重新激活时由 Application 生成的非零会话标识。
  std::uint64_t session_id{0};
  /// 同一会话内严格递增的非零序号。
  std::uint64_t sequence{0};
  /// CLOCK_MONOTONIC 截止时间，单位纳秒；普通输出命令不允许无限有效。
  std::int64_t deadline_ns{0};
  /// 要更新的普通数字输出位；位分配在逻辑消息合同冻结后定义。
  std::uint32_t mask{0};
  /// 普通数字输出目标值，只解释 mask 置位的部分。
  std::uint32_t values{0};
};

/**
 * 经典 CAN 2.0 帧的仓内搬运类型。
 * can_id 保留 Linux SocketCAN 的 EFF/RTR/ERR flag 位布局，data 未使用区域保持 0。
 * SocketCan 只搬运 flags；CAN V1 codec 再按合同拒绝 EFF/RTR/非法 ID/DLC。
 */
struct CanFrame {
  std::uint32_t can_id{0};
  std::uint8_t len{0};
  std::uint8_t data[8]{};

  [[nodiscard]] constexpr bool is_extended() const noexcept {
    return (can_id & 0x80000000u) != 0u;
  }

  [[nodiscard]] constexpr bool is_rtr() const noexcept {
    return (can_id & 0x40000000u) != 0u;
  }

  [[nodiscard]] constexpr std::uint32_t id() const noexcept {
    return can_id & 0x1FFFFFFFu;
  }
};

}  // namespace rcr
