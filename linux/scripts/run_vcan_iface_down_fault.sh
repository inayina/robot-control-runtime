#!/usr/bin/env bash
# 阶段 B：显式授权的 vcan 接口 down 故障实验。
# 会执行 `ip link set <iface> down/up`，需要 root 或 CAP_NET_ADMIN。
# Runtime 库本身不改链路；本脚本是唯一推荐的运维入口。
#
# 默认 CTest 在未设置 RCR_ALLOW_IFACE_DOWN 时 Skip 该场景，避免误改主机网络。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IFACE="${1:-vcan0}"
BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/linux}"
TEST_BIN="${BUILD_DIR}/tests/test_runtime_daemon"

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo $0 ${IFACE}" >&2
  echo "hint: this mutates host link state; default CTest skips without authorization." >&2
  exit 1
fi

if [[ ! -x "${TEST_BIN}" ]]; then
  echo "error: missing ${TEST_BIN}; build first:" >&2
  echo "  cmake -S linux -B ${BUILD_DIR} && cmake --build ${BUILD_DIR} -j --target test_runtime_daemon" >&2
  exit 1
fi

if [[ ! -e "/sys/class/net/${IFACE}" ]]; then
  echo "error: ${IFACE} missing; create with: sudo ./linux/scripts/setup_vcan.sh ${IFACE}" >&2
  exit 1
fi

ip link set dev "${IFACE}" up
export RCR_ALLOW_IFACE_DOWN=1
export RCR_NODE_SIM="${BUILD_DIR}/rcr_node_sim"

cd "${BUILD_DIR}/tests"
# test_runtime_daemon 内含多个用例；仅 DaemonVcanInterfaceDownPropagatesIoError
# 会消费 RCR_ALLOW_IFACE_DOWN。整文件跑完可顺带确认恢复后其它用例仍绿。
set +e
./test_runtime_daemon
STATUS=$?
set -e

ip link set dev "${IFACE}" up || true
ip -br link show "${IFACE}" || true
exit "${STATUS}"
