#!/usr/bin/env bash
# P3-A1：对本仓库中的 unit 做 systemd-analyze 静态验证。
# 仓库内 unit 仍写死 /opt/robot-control-runtime/current（部署合同）。
# 验证时把 ExecStart 临时改写到 stub，避免“尚未 install release”被误判成 unit 语法错误。
# 本报告不能写成 Orange Pi 实机证据。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
UNIT_DIR="${ROOT}/deploy/systemd"
OUT_DIR="${ROOT}/evidence/systemd"
STAMP="$(date -u +%Y%m%dT%H%M%SZ).$$"
REPORT="${OUT_DIR}/analyze_verify_${STAMP}.txt"
REPORT_TMP="${OUT_DIR}/.analyze_verify_${STAMP}.$$.tmp"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rcr_systemd_verify.XXXXXX")"

cleanup() {
  rm -rf "${WORKDIR}"
  rm -f "${REPORT_TMP}"
}
trap cleanup EXIT

if ! command -v systemd-analyze >/dev/null 2>&1; then
  echo "error: systemd-analyze not found" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"
if [[ -e "${REPORT}" ]]; then
  echo "error: refuse overwrite ${REPORT}" >&2
  exit 1
fi

UNITS_SRC=(
  "${UNIT_DIR}/rcr-vcan.service"
  "${UNIT_DIR}/rcrd.service"
  "${UNIT_DIR}/rcr-node-sim.service"
)

for unit in "${UNITS_SRC[@]}"; do
  [[ -f "${unit}" ]] || {
    echo "error: missing ${unit}" >&2
    exit 1
  }
done

STUB_PREFIX="${WORKDIR}/opt/robot-control-runtime/current"
mkdir -p "${STUB_PREFIX}/bin"
for name in rcrd rcr_node_sim setup_vcan.sh; do
  # stub 只需可执行；analyze 检查路径存在性，不执行业务。
  printf '#!/bin/sh\nexit 0\n' >"${STUB_PREFIX}/bin/${name}"
  chmod 0755 "${STUB_PREFIX}/bin/${name}"
done

VERIFY_UNITS=()
for src in "${UNITS_SRC[@]}"; do
  base="$(basename "${src}")"
  dst="${WORKDIR}/${base}"
  # 仅验证副本改路径；仓库原文保持部署合同绝对路径。
  sed "s|/opt/robot-control-runtime/current|${STUB_PREFIX}|g" "${src}" >"${dst}"
  VERIFY_UNITS+=("${dst}")
done

{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "machine=$(uname -m)"
  echo "systemd_analyze=$(systemd-analyze --version | head -1)"
  echo "git_commit=$(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)"
  if [[ -n "$(git -C "${ROOT}" status --porcelain 2>/dev/null)" ]]; then
    echo "git_dirty=true"
  else
    echo "git_dirty=false"
  fi
  echo "platform_claim=thinkpad_or_dev_host_static_only"
  echo "orange_pi_evidence=false"
  echo "stub_prefix=${STUB_PREFIX}"
  echo "note=verify uses path-rewritten copies; shipped units keep /opt/.../current"
  echo "command=systemd-analyze verify <rewritten units>"
  echo "----- verify output -----"
} >"${REPORT_TMP}"

set +e
systemd-analyze verify "${VERIFY_UNITS[@]}" >>"${REPORT_TMP}" 2>&1
STATUS=$?
set -e

{
  echo "----- end -----"
  echo "exit_code=${STATUS}"
  if [[ "${STATUS}" -eq 0 ]]; then
    echo "result=pass"
  else
    echo "result=failed"
  fi
} >>"${REPORT_TMP}"

mv -f "${REPORT_TMP}" "${REPORT}"
echo "wrote ${REPORT}"
exit "${STATUS}"
