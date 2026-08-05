#!/usr/bin/env bash
# 在 Orange Pi 上以 root 跑完整 12 格矩阵（含 SCHED_FIFO），再把证据交回 orangepi。
# 本脚本不修改二进制 capability；非 root + cap_sys_nice 是另一项权限实验。
# 用法（板上）：
#   sudo bash deploy/orangepi/b3_fifo_matrix_once.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OWNER="${SUDO_USER:-orangepi}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo bash $0" >&2
  exit 1
fi

cd "${ROOT}"
export RCR_BENCH_DURATION_MS="${RCR_BENCH_DURATION_MS:-5000}"
export RCR_BENCH_AFFINITY="${RCR_BENCH_AFFINITY:-0}"
export RCR_BENCH_FIFO_PRIORITY="${RCR_BENCH_FIFO_PRIORITY:-10}"

echo "privilege_model=root_for_matrix; binary_capability_not_modified"
./linux/scripts/run_orangepi_benchmark_matrix.sh

chown -R "${OWNER}:${OWNER}" "${ROOT}/evidence/orangepi_baseline"
echo "FIFO matrix done; ownership restored to ${OWNER}"
