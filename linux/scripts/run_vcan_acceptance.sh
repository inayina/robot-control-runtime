#!/usr/bin/env bash
# 双进程 vcan 阶段验收入口。缺少 CAN 接口时失败（不是 Skip）。
# 证据仅为软件 SocketCAN/vcan 路径，不能当作物理 CAN 台架结果。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IFACE="${1:-vcan0}"
BUILD_DIR="${RCR_BUILD_DIR:-$ROOT/build/linux}"
ACC="$BUILD_DIR/rcr_vcan_acceptance"
SIM="$BUILD_DIR/rcr_node_sim"
EVIDENCE_DIR="$ROOT/evidence/vcan_acceptance"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
EVIDENCE_FILE="$EVIDENCE_DIR/${STAMP}_${IFACE}.txt"

if [[ ! -x "$ACC" || ! -x "$SIM" ]]; then
  echo "error: build acceptance binaries first:" >&2
  echo "  cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug && cmake --build build/linux -j" >&2
  exit 1
fi

if [[ ! -e "/sys/class/net/${IFACE}/type" ]] || [[ "$(cat "/sys/class/net/${IFACE}/type")" != "280" ]]; then
  echo "error: ${IFACE} is not an available CAN interface" >&2
  echo "run: sudo $ROOT/linux/scripts/setup_vcan.sh ${IFACE}" >&2
  exit 1
fi

mkdir -p "$EVIDENCE_DIR"
# 验收程序从当前目录查询 Git commit 和 dirty 状态。固定切换到仓库根目录，避免用户
# 从其他目录调用脚本时错误记录另一个仓库（或记录 unknown）。
cd "$ROOT"
echo "running: $ACC --can ${IFACE} --sim-path $SIM --evidence $EVIDENCE_FILE"
"$ACC" --can "${IFACE}" --sim-path "$SIM" --evidence "$EVIDENCE_FILE"
echo "evidence: $EVIDENCE_FILE"
