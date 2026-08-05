#!/usr/bin/env bash
# Orange Pi：root 跑 RT2 四代表条件 cyclictest（约 4×60s）。
# 用法：
#   sudo bash deploy/orangepi/rt2_cyclictest_once.sh
# dirty 树：
#   sudo RCR_RT2_ALLOW_DIRTY=1 bash deploy/orangepi/rt2_cyclictest_once.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OWNER="${SUDO_USER:-orangepi}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo bash $0" >&2
  exit 1
fi

cd "${ROOT}"
export RCR_RT2_ALLOW_DIRTY="${RCR_RT2_ALLOW_DIRTY:-0}"
export RCR_RT2_DURATION_S="${RCR_RT2_DURATION_S:-60}"
export RCR_RT2_FIFO_PRIORITY="${RCR_RT2_FIFO_PRIORITY:-10}"

echo "privilege_model=root_for_cyclictest_policy_and_governor"
echo "expected_wall_time_s=$(( ${RCR_RT2_DURATION_S} * 4 + 30 ))"
./linux/scripts/run_realtime_linux_rt2.sh

chown -R "${OWNER}:${OWNER}" "${ROOT}/evidence/realtime_linux"
echo "RT2 cyclictest done; ownership -> ${OWNER}"
