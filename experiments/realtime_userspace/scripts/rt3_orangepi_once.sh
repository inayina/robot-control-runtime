#!/usr/bin/env bash
# Orange Pi：root 跑完整 RT3（含 FIFO PI 对照）。
# 用法：sudo bash experiments/realtime_userspace/scripts/rt3_orangepi_once.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OWNER="${SUDO_USER:-orangepi}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo bash $0" >&2
  exit 1
fi

cd "${ROOT}"
export RCR_RT3_BUILD="${RCR_RT3_BUILD:-${ROOT}/build/rt3}"
if [[ ! -x "${RCR_RT3_BUILD}/rcr_rt3_mlock" ]]; then
  cmake -S experiments/realtime_userspace -B "${RCR_RT3_BUILD}" -DCMAKE_BUILD_TYPE=Release
  cmake --build "${RCR_RT3_BUILD}" -j"$(nproc)"
fi

./experiments/realtime_userspace/scripts/run_rt3_once.sh
chown -R "${OWNER}:${OWNER}" "${ROOT}/evidence/realtime_linux"
echo "RT3 Orange Pi run done; ownership -> ${OWNER}"
