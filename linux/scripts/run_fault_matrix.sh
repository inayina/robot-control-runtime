#!/usr/bin/env bash
# 自动故障矩阵。缺 vcan0 硬失败。证据拒绝覆盖。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/linux}"
IFACE="${1:-vcan0}"
EVIDENCE_DIR="${ROOT}/evidence/fault_matrix"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
EVIDENCE_FILE="${EVIDENCE_DIR}/${STAMP}_${IFACE}.txt"

MATRIX="${BUILD_DIR}/rcr_fault_matrix"
SIM="${BUILD_DIR}/rcr_node_sim"
RCRD="${BUILD_DIR}/rcrd"

if [[ ! -x "${MATRIX}" || ! -x "${SIM}" || ! -x "${RCRD}" ]]; then
  echo "error: build first:" >&2
  echo "  cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug && cmake --build build/linux -j" >&2
  exit 1
fi

mkdir -p "${EVIDENCE_DIR}"
cd "${ROOT}"
echo "running fault matrix on ${IFACE}"
"${MATRIX}" --can "${IFACE}" --sim-path "${SIM}" --rcrd-path "${RCRD}" --evidence "${EVIDENCE_FILE}"
echo "evidence: ${EVIDENCE_FILE}"
