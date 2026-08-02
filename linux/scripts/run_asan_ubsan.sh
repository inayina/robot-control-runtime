#!/usr/bin/env bash
# ASan + UBSan：独立 build 目录，不污染普通 benchmark 二进制。
# LeakSanitizer 在不支持 ptrace 的环境关闭，并写入报告。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

BUILD_DIR="${RCR_ASAN_BUILD_DIR:-${ROOT}/build/linux-asan}"
EVIDENCE_DIR="${ROOT}/evidence/sanitizer"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
REPORT="${EVIDENCE_DIR}/asan_ubsan_${STAMP}.txt"

mkdir -p "${EVIDENCE_DIR}"
if [[ -e "${REPORT}" ]]; then
  echo "error: refuse overwrite ${REPORT}" >&2
  exit 1
fi

cd "${ROOT}"
cmake -S linux -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DRCR_ENABLE_ASAN=ON -DRCR_ENABLE_TSAN=OFF
cmake --build "${BUILD_DIR}" -j

# 许多受限环境无法启用 LSan；显式关闭并记录，避免假 FAIL。
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

set +e
ctest --test-dir "${BUILD_DIR}" --output-on-failure | tee /tmp/rcr_asan_ctest.txt
CTEST_RC=${PIPESTATUS[0]}
set -e

{
  rcr_write_environment /tmp/rcr_asan_env.txt "${BUILD_DIR}" Debug
  cat /tmp/rcr_asan_env.txt
  echo "sanitizer=ASan+UBSan"
  echo "lsan=disabled_detect_leaks_0"
  echo "asan_options=${ASAN_OPTIONS}"
  echo "ubsan_options=${UBSAN_OPTIONS}"
  echo "ctest_exit_code=${CTEST_RC}"
  if [[ "${CTEST_RC}" -eq 0 ]]; then
    echo "result=pass"
  else
    echo "result=failed"
  fi
  echo "----- ctest -----"
  cat /tmp/rcr_asan_ctest.txt
} >"${REPORT}"

echo "evidence: ${REPORT}"
exit "${CTEST_RC}"
