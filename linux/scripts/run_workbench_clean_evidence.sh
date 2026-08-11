#!/usr/bin/env bash
# 从干净提交生成 Workbench Phase 3.5 软件证据。只使用 vcan；不代表物理 CAN。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

IFACE="${1:-vcan0}"
BUILD_DIR="${RCR_WORKBENCH_BUILD_DIR:-${ROOT}/build/workbench-evidence}"
ASAN_BUILD_DIR="${RCR_WORKBENCH_ASAN_BUILD_DIR:-${ROOT}/build/workbench-evidence-asan}"
EVIDENCE_ROOT="${ROOT}/evidence/workbench"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
FINAL_DIR="${EVIDENCE_ROOT}/${STAMP}"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rcr_workbench_evidence.XXXXXX")"

cleanup() {
  rm -rf "${WORKDIR}"
}
trap cleanup EXIT

if [[ -n "$(git -C "${ROOT}" status --porcelain)" ]]; then
  echo "error: Workbench clean evidence requires a clean git worktree" >&2
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

COMMIT="$(rcr_git_commit)"
mkdir -p "${WORKDIR}/results"
rcr_write_environment "${WORKDIR}/environment.txt" "${BUILD_DIR}" Debug
ip -details link show "${IFACE}" >"${WORKDIR}/vcan_interface.txt"

cd "${ROOT}"
cmake -S linux -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DRCR_BUILD_TESTS=ON
cmake --build "${BUILD_DIR}" -j2
ctest --test-dir "${BUILD_DIR}" --output-on-failure |
  tee "${WORKDIR}/ctest_all.txt"

RCR_WORKBENCH_RESULT_DIR="${WORKDIR}/results" \
RCR_WORKBENCH_GIT_COMMIT="${COMMIT}" \
RCR_WORKBENCH_BUILD_TYPE="Debug" \
  ctest --test-dir "${BUILD_DIR}" \
    -R '^test_workbench_can_health_vcan$' -V |
  tee "${WORKDIR}/can_health_vcan.txt"

cmake -S linux -B "${ASAN_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON -DRCR_ENABLE_ASAN=ON -DRCR_ENABLE_TSAN=OFF
cmake --build "${ASAN_BUILD_DIR}" --target \
  test_workbench_runner test_workbench_runtime_adapter \
  test_workbench_can_health test_workbench_result_writer \
  test_workbench_can_health_vcan -j2
ASAN_OPTIONS="detect_leaks=0:abort_on_error=1" \
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
  ctest --test-dir "${ASAN_BUILD_DIR}" \
    -R '^test_workbench_(runner|runtime_adapter|can_health|result_writer|can_health_vcan)$' \
    --output-on-failure | tee "${WORKDIR}/asan_ubsan.txt"

{
  echo "phase=workbench_phase_3_5_clean_evidence"
  echo "result=pass"
  echo "evidence_class=VCAN"
  echo "physical_can=not_run"
  echo "qt=not_run"
  echo "git_commit=${COMMIT}"
  echo "git_dirty=false"
  echo "interface=${IFACE}"
  echo "result_schema=rcr.workbench.result.v1"
} >"${WORKDIR}/manifest.txt"

(cd "${WORKDIR}" && sha256sum environment.txt vcan_interface.txt \
  ctest_all.txt can_health_vcan.txt asan_ubsan.txt manifest.txt \
  results/*.json results/*.csv >sha256sums.txt)

mkdir -p "${EVIDENCE_ROOT}"
mv "${WORKDIR}" "${FINAL_DIR}"
trap - EXIT
echo "evidence: ${FINAL_DIR}"
