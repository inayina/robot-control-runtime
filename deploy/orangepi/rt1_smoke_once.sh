#!/usr/bin/env bash
# Orange Pi 上以 root 跑 RT1 60 秒 smoke（10 格，确认 affinity/FIFO/governor/压力生效）。
# 正式 30 分钟矩阵须在用户审阅 smoke 后再跑 rt1_formal_once.sh。
#
# 用法（板上 clean 工作树 + Release 构建后）：
#   sudo bash deploy/orangepi/rt1_smoke_once.sh
# dirty 调试（不得当 baseline）：
#   sudo RCR_RT1_ALLOW_DIRTY=1 bash deploy/orangepi/rt1_smoke_once.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OWNER="${SUDO_USER:-orangepi}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo bash $0" >&2
  exit 1
fi

cd "${ROOT}"
export RCR_RT1_MODE=smoke
export RCR_RT1_FIFO_PRIORITY="${RCR_RT1_FIFO_PRIORITY:-10}"
export RCR_RT1_CROSS_5MS="${RCR_RT1_CROSS_5MS:-0}"
# 允许调用方传入 ALLOW_DIRTY / DURATION_MS / BUILD_DIR
export RCR_RT1_ALLOW_DIRTY="${RCR_RT1_ALLOW_DIRTY:-0}"
export RCR_BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/linux}"

echo "privilege_model=root_for_fifo_and_governor; binary_capability_not_modified"
echo "mode=smoke duration_default_ms=60000"
./linux/scripts/run_realtime_linux_rt1.sh

chown -R "${OWNER}:${OWNER}" "${ROOT}/evidence/realtime_linux"
echo "RT1 smoke done; ownership restored to ${OWNER}"
echo "Review evidence/realtime_linux/*_rt1_smoke/summary.txt before formal."
