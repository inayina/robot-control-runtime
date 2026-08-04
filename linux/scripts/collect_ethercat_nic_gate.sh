#!/usr/bin/env bash
# =============================================================================
# EtherCAT NIC Gate 主机探测（可复跑；一次结果不永久成立）
#
# 解决什么问题：
#   买 SubDevice 之前，用一份可审查的快照回答：这张 ThinkPad 有线口能否当 SOEM
#   用户态主站的**候选口**（身份、raw、路由、NM、适配器打开路径）。
#
# 为什么这样测（对应 docs/ETHERCAT_NIC_GATE.md G1–G5）：
#   G1 写 PCI/驱动/接口名 —— 换内核或笔记本后能对照，避免“以前那张口”说不清；
#   G2 raw bind 0x88A4 —— 无从站时也能验证权限与 CONFIG_PACKET；不发业务 PDO；
#   G3 默认路由 —— 管理流量应在 Wi-Fi，实验口留给 EtherCAT；
#   G4 NM managed 字段 —— carrier 上来后 DHCP 抢口是常见假故障；
#   G5 SOEM slaveinfo —— 只证明 ecx_init 打开适配器；无从站时 No slaves 常为预期。
#
# 不能从本脚本声称：
#   SubDevice 进 OP、PDO、WKC、周期确定性、PREEMPT_RT、功能安全；
#   一次 probe_* 在换硬件/内核后仍成立（须复跑并换时间戳目录）。
#
# 用法：
#   ./linux/scripts/collect_ethercat_nic_gate.sh
#   sudo ./linux/scripts/collect_ethercat_nic_gate.sh
#   sudo ./linux/scripts/collect_ethercat_nic_gate.sh --iface enp0s31f6
#
# 无 root：写 PCI/驱动/路由/NM 只读摘要；raw 记 permission_denied（G2 不可写通过）。
# 有 root：额外测 AF_PACKET bind(EtherType 0x88A4)、nmcli 详情、可选 slaveinfo 空扫。
#
# 合同：docs/ETHERCAT_NIC_GATE.md ；协议预习：docs/ETHERCAT_PROTOCOL_NOTES.md
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IFACE="${RCR_ECAT_IFACE:-enp0s31f6}"
MGMT_HINT="${RCR_ECAT_MGMT_IFACE:-wlp0s20f3}"
SOEM_SLAVEINFO="${ROOT}/SOEM/build/samples/slaveinfo/slaveinfo"
OUT_ROOT="${ROOT}/evidence/ethercat_nic_gate"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="${OUT_ROOT}/probe_${STAMP}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --iface) IFACE="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,30p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

mkdir -p "${OUT_DIR}"
EUID_NOW="$(id -u)"
HAVE_ROOT=0
[[ "${EUID_NOW}" -eq 0 ]] && HAVE_ROOT=1

# --- environment：把“在哪台机、哪次代码、是否脏树”钉进快照（G6 要看 git_dirty）---
{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "os_kernel=$(uname -srm)"
  echo "git_commit=$(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unavailable)"
  if git -C "${ROOT}" status --porcelain 2>/dev/null | grep -q .; then
    echo "git_dirty=true"
  else
    echo "git_dirty=false"
  fi
  echo "collector=linux/scripts/collect_ethercat_nic_gate.sh"
  echo "iface=${IFACE}"
  echo "mgmt_iface_hint=${MGMT_HINT}"
  echo "ran_as_uid=${EUID_NOW}"
  echo "have_root=${HAVE_ROOT}"
  echo "note=One probe is a dated snapshot; re-run after kernel/NIC/NM change."
} >"${OUT_DIR}/environment.txt"

# --- G1：网卡身份（PCI / 驱动 / 链路）；换机对照靠这里，不是靠口头记忆 ---
{
  echo "=== ip link ==="
  ip -details -statistics link show dev "${IFACE}" 2>&1 || true
  echo
  echo "=== ip addr ==="
  ip -4 addr show dev "${IFACE}" 2>&1 || true
  ip -6 addr show dev "${IFACE}" 2>&1 | head -20 || true
  echo
  echo "=== ethtool -i ==="
  ethtool -i "${IFACE}" 2>&1 || true
  echo
  echo "=== ethtool settings (may need CAP_NET_ADMIN) ==="
  ethtool "${IFACE}" 2>&1 || true
  echo
  echo "=== sysfs pci ==="
  if [[ -e "/sys/class/net/${IFACE}/device" ]]; then
    DEV="$(readlink -f "/sys/class/net/${IFACE}/device")"
    echo "device_path=${DEV}"
    cat "${DEV}/uevent" 2>/dev/null || true
  else
    echo "device_path=unavailable"
  fi
  echo
  echo "=== lspci ethernet ==="
  lspci -nnk 2>/dev/null | awk '
    /Ethernet controller/ {p=1}
    p {
      print
      if ($0 ~ /Kernel modules:/) {p=0; print ""}
    }' || true
  echo
  echo "=== modinfo e1000e (summary) ==="
  modinfo e1000e 2>/dev/null | egrep '^(filename|version|vermagic|srcversion|description)=' || \
    modinfo e1000e 2>/dev/null | egrep '^(filename|version|vermagic|srcversion|description)' || true
} >"${OUT_DIR}/nic_identity.txt"

# --- G3：默认路由是否仍在管理面 Wi-Fi（实验口不应变成上网出口）---
{
  echo "=== default routes ==="
  ip route show default 2>&1 || true
  echo
  echo "=== wifi / mgmt addr ==="
  ip -4 addr show dev "${MGMT_HINT}" 2>&1 || true
  echo
  echo "=== all ipv4 routes (head) ==="
  ip -4 route | head -40
} >"${OUT_DIR}/routing.txt"

# --- G4 只读侧：NM 是否托管实验口；并记录 CONFIG_PACKET（raw 的前提）---
{
  echo "=== nmcli device status ==="
  nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status 2>&1 || echo "nmcli_unavailable"
  echo
  echo "=== nmcli device show ${IFACE} ==="
  nmcli device show "${IFACE}" 2>&1 || true
  echo
  echo "=== nmcli connections (ethernet) ==="
  nmcli -t -f NAME,UUID,TYPE,DEVICE,AUTOCONNECT connection show 2>&1 | grep -iE 'ethernet|802-3' || true
  echo
  echo "=== kernel CONFIG_PACKET ==="
  if [[ -r /proc/config.gz ]]; then
    zgrep '^CONFIG_PACKET' /proc/config.gz || true
  else
    grep '^CONFIG_PACKET' "/boot/config-$(uname -r)" 2>/dev/null || echo "unavailable"
  fi
} >"${OUT_DIR}/nm_and_packet.txt"

# --- G2：仅 bind，不发 PDO。pass = 能打开 AF_PACKET+0x88A4，≠ 实时周期合格 ---
RAW_RESULT="not_run"
RAW_DETAIL=""
if [[ "${HAVE_ROOT}" -eq 1 ]]; then
  RAW_OUT="$(python3 - "${IFACE}" <<'PY' 2>&1
import errno, os, socket, sys
iface = sys.argv[1]
ETH_P_ECAT = 0x88A4
try:
    # EtherCAT 帧用 EtherType 0x88A4；从站 ESC 认的是二层帧，不是 TCP payload。
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ECAT))
    s.bind((iface, 0))
    print(f"result=pass")
    print(f"detail=AF_PACKET SOCK_RAW bind EtherType=0x88A4 iface={iface} fd={s.fileno()} uid={os.geteuid()}")
    s.close()
except OSError as e:
    code = errno.errorcode.get(e.errno, str(e.errno))
    if e.errno == errno.EPERM:
        print("result=permission_denied")
    else:
        print("result=failed")
    print(f"detail=errno={e.errno} ({code}) {e}")
    sys.exit(0)
PY
)" || true
  RAW_RESULT="$(printf '%s\n' "${RAW_OUT}" | awk -F= '/^result=/{print $2; exit}')"
  RAW_DETAIL="$(printf '%s\n' "${RAW_OUT}" | awk -F= '/^detail=/{sub(/^detail=/,""); print; exit}')"
else
  RAW_RESULT="permission_denied"
  RAW_DETAIL="ran without root/CAP_NET_RAW; re-run under sudo"
fi

{
  echo "result=${RAW_RESULT}"
  echo "iface=${IFACE}"
  echo "ethertype=0x88A4"
  echo "detail=${RAW_DETAIL}"
  echo "note=Bind-only smoke test; does not prove EtherCAT cycle, WKC, or driver realtime."
} >"${OUT_DIR}/raw_frame.txt"

# --- G5：SOEM 打开适配器；空扫成功 ≠ 有从站，更 ≠ PDO/OP ---
SLAVE_RESULT="not_run"
if [[ "${HAVE_ROOT}" -eq 1 && -x "${SOEM_SLAVEINFO}" ]]; then
  set +e
  SLAVE_LOG="$(timeout 5s "${SOEM_SLAVEINFO}" "${IFACE}" 2>&1)"
  SLAVE_EC=$?
  set -e
  {
    echo "command=${SOEM_SLAVEINFO} ${IFACE}"
    echo "exit_code=${SLAVE_EC}"
    echo "timeout_sec=5"
    echo "note=No SubDevice expected yet; empty scan or link-down is informative, not Gate PASS for PDO."
    echo "----- stdout/stderr -----"
    printf '%s\n' "${SLAVE_LOG}"
  } >"${OUT_DIR}/soem_slaveinfo.txt"
  if [[ "${SLAVE_EC}" -eq 0 ]]; then
    SLAVE_RESULT="pass"
  else
    SLAVE_RESULT="failed"
  fi
else
  {
    echo "result=not_run"
    if [[ ! -x "${SOEM_SLAVEINFO}" ]]; then
      echo "detail=SOEM slaveinfo binary missing at ${SOEM_SLAVEINFO}"
    else
      echo "detail=needs root to open adapter for scan"
    fi
  } >"${OUT_DIR}/soem_slaveinfo.txt"
fi

# Summary：机器可读摘要。NM 是否 unmanaged 由探测结果判定，不在此脚本“发明 PASS”。
NM_MANAGED="$(nmcli -g GENERAL.NM-MANAGED device show "${IFACE}" 2>/dev/null || echo unavailable)"
NM_STATE="$(nmcli -g GENERAL.STATE device show "${IFACE}" 2>/dev/null || echo unavailable)"
DEFAULT_DEV="$(ip route show default 2>/dev/null | awk '/^default/ {print $5; exit}')"

NM_UNMANAGED="not_run"
case "${NM_MANAGED}" in
  no|No|FALSE|false|0)
    NM_UNMANAGED="pass"
    ;;
  yes|Yes|TRUE|true|1)
    NM_UNMANAGED="failed"
    ;;
  *)
    # Fall back on state string from nmcli when NM-MANAGED field empty
    if [[ "${NM_STATE}" == *unmanaged* ]]; then
      NM_UNMANAGED="pass"
      NM_MANAGED="no"
    else
      NM_UNMANAGED="not_run"
    fi
    ;;
esac

{
  echo "gate=EtherCAT_NIC_preconditions"
  echo "snapshot_dir=${OUT_DIR}"
  echo "iface=${IFACE}"
  echo "nic_identity=recorded"
  echo "raw_frame_bind=${RAW_RESULT}"
  echo "soem_slaveinfo=${SLAVE_RESULT}"
  echo "nm_managed=${NM_MANAGED}"
  echo "nm_state=${NM_STATE}"
  echo "default_route_dev=${DEFAULT_DEV:-none}"
  if [[ "${DEFAULT_DEV:-}" == "${MGMT_HINT}" ]]; then
    echo "mgmt_via_wifi_hint=yes"
  elif [[ -n "${DEFAULT_DEV:-}" ]]; then
    echo "mgmt_via_wifi_hint=check_routing_txt"
  else
    echo "mgmt_via_wifi_hint=no_default_route"
  fi
  echo "nm_unmanaged_for_ethercat=${NM_UNMANAGED}"
  echo "note_nm=If nm_unmanaged_for_ethercat=failed, apply deploy/ethercat/apply_nm_unmanaged.sh --apply before plugging a SubDevice."
  echo "cannot_claim=SubDevice OP, PDO, WKC, cycle determinism, or permanent NIC capability"
} >"${OUT_DIR}/SUMMARY.txt"

echo "wrote ${OUT_DIR}"
echo "raw_frame_bind=${RAW_RESULT}  (need sudo for pass path)"
echo "Next: review SUMMARY.txt; if NM manages ${IFACE}, set it unmanaged before plugging an EtherCAT slave."
