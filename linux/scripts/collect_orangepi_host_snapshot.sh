#!/usr/bin/env bash
# 采集板上可自动读取的主机快照（P3-A2 模板配套；到货后 B0 使用）。
# 不修改 governor / 权限；不把 NOT_OBSERVED 项填成假 PASS。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

OUT_ROOT="${ROOT}/evidence/orangepi"
STAMP="$(date -u +%Y%m%dT%H%M%SZ).$$"
OUT_DIR="${OUT_ROOT}/host_snapshot_${STAMP}"

mkdir -p "${OUT_DIR}"
rcr_write_environment "${OUT_DIR}/environment.txt" "${RCR_BUILD_DIR:-}" "${RCR_BUILD_TYPE:-}"
rcr_write_board_snapshot "${OUT_DIR}/board_snapshot.txt"

{
  echo "lscpu:"
  lscpu 2>/dev/null || echo "unavailable"
  echo
  echo "cpufreq policies:"
  for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    [[ -e "${policy}" ]] || continue
    printf '%s related_cpus=' "${policy}"
    cat "${policy}/related_cpus" 2>/dev/null || echo unavailable
    printf ' governor='
    cat "${policy}/scaling_governor" 2>/dev/null || echo unavailable
  done
  echo
  echo "online cpus:"
  cat /sys/devices/system/cpu/online 2>/dev/null || echo unavailable
} >"${OUT_DIR}/cpu_topology.txt"

echo "wrote ${OUT_DIR}"
echo "Fill deploy/orangepi/BRINGUP_CHECKLIST.md observed/result fields next; do not pre-fill PASS."
