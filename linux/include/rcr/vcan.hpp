#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include <string>
#include <string_view>

namespace rcr {

/**
 * Linux CAN 接口只读探测。
 *
 * 创建/启用接口不属于 Runtime 库职责：库不得调用 shell 或修改宿主网络。
 * 运维入口是 linux/scripts/setup_vcan.sh（需要 root 或 CAP_NET_ADMIN）。
 * vcan 只能验证 SocketCAN 软件路径，不能证明物理 CAN 波形或端接正确。
 */
enum class CanInterfaceStatus {
  /// /sys/class/net/<name> 存在且 type 为 ARPHRD_CAN。
  Available = 0,
  /// 同名网络接口目录不存在。
  Missing = 1,
  /// 接口存在，但不是 CAN（例如以太网同名接口）。
  NotCan = 2,
  /// 空名、含路径分隔符或不符合接口名白名单。
  InvalidName = 3,
};

[[nodiscard]] CanInterfaceStatus probe_can_interface(std::string_view ifname);

/// Available 为 true；Missing / NotCan / InvalidName 均为 false。
[[nodiscard]] bool can_interface_available(std::string_view ifname = "vcan0");

[[nodiscard]] bool net_interface_exists(std::string_view ifname);

[[nodiscard]] std::string default_vcan_name();

}  // namespace rcr
