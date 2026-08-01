// Orange Pi/Linux Runtime 实现；不得加入 MCU HAL、FreeRTOS 或 ESP-IDF 依赖。
#include "rcr/vcan.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace rcr {
namespace {

// linux/if_arp.h ARPHRD_CAN；这里写死常量，避免把 if_arp.h 拉进探测路径。
constexpr int kArphrdCan = 280;

bool is_valid_ifname(std::string_view ifname) {
  // 严格白名单：与 setup_vcan.sh 一致，拒绝路径与 shell 元字符。
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
    return CanInterfaceStatus::Missing;
  }

  // type 文件是内核 ARPHRD_* 值；仅存在同名 net 目录不足以证明是 CAN。
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
