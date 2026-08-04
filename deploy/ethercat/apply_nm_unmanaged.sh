#!/usr/bin/env bash
# =============================================================================
# 将 EtherCAT 实验口交给主站独占：NM unmanaged + 关闭有线 autoconnect
#
# 解决什么问题：
#   插上 SubDevice（或任意产生 carrier 的对端）后，NetworkManager 常按
#   “Wired connection …” 自动 DHCP。主站需要的是二层 raw / SOEM，不是 IPv4。
#   DHCP 与默认路由漂移会把故障伪装成“EtherCAT 不稳”。
#
# 对应 Gate：docs/ETHERCAT_NIC_GATE.md G4（以及 G3 的路由验收）。
# 协议背景：docs/ETHERCAT_PROTOCOL_NOTES.md（AF_PACKET / 独占口）。
#
# 为何默认 dry-run：
#   会写 /etc/NetworkManager/conf.d/，属主机级变更；必须显式 --apply。
#
# 不能声称：
#   执行本脚本 = 总线协议合格 / 已进 OP；仅完成管理面隔离。
#
# 用法（需 root）：
#   sudo ./deploy/ethercat/apply_nm_unmanaged.sh            # 预览
#   sudo ./deploy/ethercat/apply_nm_unmanaged.sh --apply
#   sudo ./deploy/ethercat/apply_nm_unmanaged.sh --apply --revert
#
# 环境变量 / 参数：
#   RCR_ECAT_IFACE / --iface          实验口（默认 enp0s31f6）
#   RCR_ECAT_WIRED_CONN / --wired-conn  要关掉 autoconnect 的连接显示名
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${ROOT}/deploy/ethercat/nm/99-rcr-ethercat-unmanaged.conf"
DST="/etc/NetworkManager/conf.d/99-rcr-ethercat-unmanaged.conf"
IFACE="${RCR_ECAT_IFACE:-enp0s31f6}"
WIRED_CONN="${RCR_ECAT_WIRED_CONN:-Wired connection 1}"
APPLY=0
REVERT=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) APPLY=1; shift ;;
    --revert) REVERT=1; shift ;;
    --iface) IFACE="$2"; shift 2 ;;
    --wired-conn) WIRED_CONN="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,28p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

if [[ "$(id -u)" -ne 0 ]]; then
  echo "need root (sudo)" >&2
  exit 1
fi

if [[ ! -f "${SRC}" ]]; then
  echo "missing ${SRC}" >&2
  exit 1
fi

echo "iface=${IFACE}"
echo "wired_conn=${WIRED_CONN}"
echo "src=${SRC}"
echo "dst=${DST}"

if [[ "${REVERT}" -eq 1 ]]; then
  echo "plan: remove ${DST}; optionally re-enable autoconnect on '${WIRED_CONN}'"
  if [[ "${APPLY}" -eq 1 ]]; then
    rm -f "${DST}"
    nmcli connection modify "${WIRED_CONN}" connection.autoconnect yes 2>/dev/null || \
      echo "warn: could not modify '${WIRED_CONN}' (name may differ)"
    systemctl reload NetworkManager
    echo "reverted; verify with nmcli device show ${IFACE}"
  else
    echo "dry-run only; pass --apply to execute"
  fi
  exit 0
fi

echo "plan: install unmanaged conf; set '${WIRED_CONN}' autoconnect=no; reload NM"
if [[ "${APPLY}" -ne 1 ]]; then
  echo "--- conf preview ---"
  cat "${SRC}"
  echo "dry-run only; pass --apply to execute"
  exit 0
fi

# 1) conf.d：告诉 NM “不要管这张口”（换接口名时同步改仓内 conf 并复跑 Gate）
install -m 0644 "${SRC}" "${DST}"

# 2) 双保险：即便短暂变成 managed，也不要自动连 DHCP profile
if nmcli -t -f NAME connection show | grep -Fxq "${WIRED_CONN}"; then
  nmcli connection modify "${WIRED_CONN}" connection.autoconnect no
else
  echo "warn: connection '${WIRED_CONN}' not found; list with: nmcli connection show"
fi

# 3) 若该口上已有活跃有线 profile，先断开，避免残留 IPv4 干扰 raw 主站
ACTIVE="$(nmcli -t -f DEVICE,CONNECTION device status | awk -F: -v i="${IFACE}" '$1==i {print $2}')"
if [[ -n "${ACTIVE}" && "${ACTIVE}" != "--" ]]; then
  nmcli device disconnect "${IFACE}" || true
fi

systemctl reload NetworkManager
sleep 1
echo "=== verify ==="
nmcli -g GENERAL.NM-MANAGED,GENERAL.STATE,GENERAL.CONNECTION device show "${IFACE}" || true
nmcli -t -f NAME,AUTOCONNECT connection show "${WIRED_CONN}" 2>/dev/null || true
ip -4 addr show dev "${IFACE}" || true
ip route show default || true
echo "Next: sudo ./linux/scripts/collect_ethercat_nic_gate.sh"
echo "Expect: nm_managed=no, no IPv4 on ${IFACE}, default route still on Wi-Fi."
