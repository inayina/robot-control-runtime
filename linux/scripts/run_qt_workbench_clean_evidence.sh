#!/usr/bin/env bash
# 从干净提交生成 Phase 4 Qt Workbench 证据。只使用 vcan；不代表物理 CAN。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

IFACE="${1:-vcan0}"
QT_OFF_BUILD="${RCR_QT_OFF_BUILD_DIR:-${ROOT}/build/qt-evidence-off}"
QT_ON_BUILD="${RCR_QT_ON_BUILD_DIR:-${ROOT}/build/qt-evidence-on}"
EVIDENCE_ROOT="${ROOT}/evidence/qt_workbench"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
FINAL_DIR="${EVIDENCE_ROOT}/${STAMP}"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rcr_qt_workbench_evidence.XXXXXX")"
SIM_PID=""
COMPLETED=false

cleanup() {
  if [[ -n "${SIM_PID}" ]] && kill -0 "${SIM_PID}" 2>/dev/null; then
    kill "${SIM_PID}" 2>/dev/null || true
    wait "${SIM_PID}" 2>/dev/null || true
  fi
  if [[ "${COMPLETED}" != true ]]; then
    echo "incomplete evidence retained at: ${WORKDIR}" >&2
  fi
}
trap cleanup EXIT

if [[ -n "$(git -C "${ROOT}" status --porcelain)" ]]; then
  echo "error: Qt Workbench clean evidence requires a clean git worktree" >&2
  exit 1
fi
if [[ -e "${FINAL_DIR}" ]]; then
  echo "error: refuse overwrite ${FINAL_DIR}" >&2
  exit 1
fi
if [[ ! -e "/sys/class/net/${IFACE}/type" ]] ||
   [[ "$(cat "/sys/class/net/${IFACE}/type")" != "280" ]]; then
  echo "error: ${IFACE} is not an available CAN interface" >&2
  exit 1
fi
if ! pkg-config --atleast-version=6.4 Qt6Widgets; then
  echo "error: Qt6 Widgets >= 6.4 is required" >&2
  exit 1
fi

COMMIT="$(rcr_git_commit)"
mkdir -p "${WORKDIR}/results"
rcr_write_environment "${WORKDIR}/environment.txt" "${QT_ON_BUILD}" Debug
ip -details link show "${IFACE}" >"${WORKDIR}/vcan_interface.txt"
pkg-config --modversion Qt6Widgets >"${WORKDIR}/qt_version.txt"

cd "${ROOT}"
cmake -S linux -B "${QT_OFF_BUILD}" -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON -DRCR_BUILD_QT_DEVICE_WORKBENCH=OFF
cmake --build "${QT_OFF_BUILD}" -j2
ctest --test-dir "${QT_OFF_BUILD}" --output-on-failure |
  tee "${WORKDIR}/ctest_qt_off.txt"

cmake -S linux -B "${QT_ON_BUILD}" -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON
cmake --build "${QT_ON_BUILD}" -j2
ctest --test-dir "${QT_ON_BUILD}" --output-on-failure |
  tee "${WORKDIR}/ctest_qt_on.txt"

"${QT_ON_BUILD}/rcr_node_sim" --can "${IFACE}" --node-id 1 \
  --heartbeat-ms 20 --duration-ms 7000 >"${WORKDIR}/node_sim.txt" 2>&1 &
SIM_PID=$!
RCR_WORKBENCH_GIT_COMMIT="${COMMIT}" \
RCR_WORKBENCH_GIT_DIRTY=false \
RCR_WORKBENCH_BUILD_TYPE=Debug \
QT_QPA_PLATFORM=offscreen \
  "${QT_ON_BUILD}/tools/qt_device_workbench/rcr_qt_device_workbench" \
    --can "${IFACE}" --node-id 1 --evidence vcan \
    --results "${WORKDIR}/results" \
    --run-health-once 2>&1 | tee "${WORKDIR}/qt_offscreen.txt"
wait "${SIM_PID}"
SIM_PID=""

mapfile -t JSON_RESULTS < <(find "${WORKDIR}/results" -maxdepth 1 -type f -name '*.json')
mapfile -t CSV_RESULTS < <(find "${WORKDIR}/results" -maxdepth 1 -type f -name '*.csv')
if [[ "${#JSON_RESULTS[@]}" -ne 1 || "${#CSV_RESULTS[@]}" -ne 1 ]]; then
  echo "error: expected exactly one JSON/CSV result pair" >&2
  exit 1
fi
grep -q '"outcome": "PASS"' "${JSON_RESULTS[0]}"
grep -q '"evidence": "VCAN"' "${JSON_RESULTS[0]}"
grep -q '"quality": "SIMULATED"' "${JSON_RESULTS[0]}"
grep -q "\"git_commit\": \"${COMMIT}\"" "${JSON_RESULTS[0]}"
grep -q '"git_dirty": false' "${JSON_RESULTS[0]}"

{
  echo "phase=qt_workbench_phase_4_clean_evidence"
  echo "result=pass"
  echo "evidence_class=VCAN"
  echo "qt_version=$(cat "${WORKDIR}/qt_version.txt")"
  echo "git_commit=${COMMIT}"
  echo "git_dirty=false"
  echo "interface=${IFACE}"
  echo "physical_can=not_run"
  echo "actuator=not_run"
  echo "ipc_crash_isolation=not_implemented"
} >"${WORKDIR}/manifest.txt"

(cd "${WORKDIR}" && sha256sum environment.txt vcan_interface.txt qt_version.txt \
  ctest_qt_off.txt ctest_qt_on.txt node_sim.txt qt_offscreen.txt manifest.txt \
  results/*.json results/*.csv >sha256sums.txt)

mkdir -p "${EVIDENCE_ROOT}"
mv "${WORKDIR}" "${FINAL_DIR}"
COMPLETED=true
trap - EXIT
echo "evidence: ${FINAL_DIR}"
