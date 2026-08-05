#!/usr/bin/env bash
# Orange Pi：构建并跑 RT6 分段时延成套证据（主平台）。
# 用法：bash experiments/realtime_segmented/scripts/rt6_orangepi_once.sh
# 不需要 root（默认 OTHER）；若加 --fifo 再 sudo。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OWNER="${SUDO_USER:-$(id -un)}"
BUILD="${RCR_RT6_BUILD:-${ROOT}/build/rt6}"

cd "${ROOT}"
if [[ ! -x "${BUILD}/rcr_rt6_segments" ]]; then
  cmake -S experiments/realtime_segmented -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release
  cmake --build "${BUILD}" -j"$(nproc)"
fi

export RCR_RT6_BUILD="${BUILD}"
./experiments/realtime_segmented/scripts/run_rt6_once.sh

if [[ "${EUID}" -eq 0 ]]; then
  chown -R "${OWNER}:${OWNER}" "${ROOT}/evidence/realtime_linux" || true
fi
echo "RT6 Orange Pi run done"
