#!/usr/bin/env bash
# Orange Pi 上以 root 跑 RT1 正式 30 分钟 × 10 格（+ 可选 5ms 交叉）。
# 前置：同一 clean commit、Release 构建、smoke 已审阅。
#
# 用法：
#   sudo bash deploy/orangepi/rt1_formal_once.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OWNER="${SUDO_USER:-orangepi}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo bash $0" >&2
  exit 1
fi

cd "${ROOT}"
export RCR_RT1_MODE=formal
export RCR_RT1_FIFO_PRIORITY="${RCR_RT1_FIFO_PRIORITY:-10}"
export RCR_RT1_CROSS_5MS="${RCR_RT1_CROSS_5MS:-1}"
export RCR_RT1_ALLOW_DIRTY="${RCR_RT1_ALLOW_DIRTY:-0}"
export RCR_BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/linux}"

if [[ "${RCR_RT1_ALLOW_DIRTY}" == "1" ]]; then
  echo "error: formal baseline refuses RCR_RT1_ALLOW_DIRTY=1" >&2
  exit 1
fi

echo "privilege_model=root_for_fifo_and_governor; binary_capability_not_modified"
echo "mode=formal duration_default_ms=1800000 (~5h for 10 cells + cross)"
./linux/scripts/run_realtime_linux_rt1.sh

chown -R "${OWNER}:${OWNER}" "${ROOT}/evidence/realtime_linux"
echo "RT1 formal done; ownership restored to ${OWNER}"
