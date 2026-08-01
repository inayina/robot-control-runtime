// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/vcan.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace rcr {
namespace {

// sysfs 的 /sys/class/net/<if>/type 暴露 ARPHRD_* 数值；280 对应 ARPHRD_CAN。
// 这里写死合同常量，避免只为一个值把 Linux if_arp 宏带进公开头文件。
constexpr int kArphrdCan = 280;

bool is_valid_ifname(std::string_view ifname) {
  // Linux IFNAMSIZ 包含结尾 NUL，所以可见名称最多 15 字节。严格白名单与运维脚本一致，
  // 既阻止 "../" 逃离 sysfs 目录，也避免接口名在以后传给命令行工具时产生歧义。
  if (ifname.empty() || ifname.size() > 15) {
    return false;
  }
  for (const char ch : ifname) {
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool net_interface_exists(std::string_view ifname) {
  if (!is_valid_ifname(ifname)) {
    return false;
  }
  const std::filesystem::path path =
      std::filesystem::path("/sys/class/net") / std::string(ifname);
  std::error_code ec;
  // 使用 error_code 重载避免权限/瞬时文件系统错误以异常越过 Runtime 边界；错误统一视为不存在。
  return std::filesystem::exists(path, ec);
}

CanInterfaceStatus probe_can_interface(std::string_view ifname) {
  if (!is_valid_ifname(ifname)) {
    return CanInterfaceStatus::InvalidName;
  }
  const std::filesystem::path net_dir =
      std::filesystem::path("/sys/class/net") / std::string(ifname);
  std::error_code ec;
  if (!std::filesystem::exists(net_dir, ec)) {
    // sysfs 是内核当前 netdevice 状态的视图；目录缺失与“存在但类型错误”需要分开诊断。
    return CanInterfaceStatus::Missing;
  }

  // type 文件是内核 ARPHRD_* 值；仅存在同名 net 目录不足以证明是 CAN。例如 lo/以太网
  // 同样位于 /sys/class/net，但不能 bind CAN_RAW。读取失败按 NotCan fail closed。
  std::ifstream type_file(net_dir / "type");
  int type_value = 0;
  if (!(type_file >> type_value)) {
    return CanInterfaceStatus::NotCan;
  }
  if (type_value != kArphrdCan) {
    return CanInterfaceStatus::NotCan;
  }
  return CanInterfaceStatus::Available;
}

bool can_interface_available(std::string_view ifname) {
  return probe_can_interface(ifname) == CanInterfaceStatus::Available;
}

std::string default_vcan_name() { return "vcan0"; }

}  // namespace rcr
