#!/usr/bin/env bash
# ASan + UBSan：独立 build 目录，不污染普通 benchmark 二进制。
# LeakSanitizer 在不支持 ptrace 的环境关闭，并写入报告。
#
# 中间文件放在本次 mktemp -d 目录；正式报告先写同目录 .tmp，字段完整后
# rename。禁止固定 /tmp 路径：第二次运行会因 refuse overwrite 失败，而外层
# 重定向已把正式报告截成 0 字节。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

BUILD_DIR="${RCR_ASAN_BUILD_DIR:-${ROOT}/build/linux-asan}"
EVIDENCE_DIR="${ROOT}/evidence/sanitizer"
# 秒精度 + PID：同秒连续/并发重跑不得撞名；撞名会 refuse overwrite，不是空报告。
STAMP="$(date -u +%Y%m%dT%H%M%SZ).$$"
REPORT="${EVIDENCE_DIR}/asan_ubsan_${STAMP}.txt"
# 与最终报告同目录，保证 rename 在同一文件系统上原子可见。
REPORT_TMP="${EVIDENCE_DIR}/.asan_ubsan_${STAMP}.$$.tmp"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rcr_asan.XXXXXX")"

cleanup() {
  rm -rf "${WORKDIR}"
  rm -f "${REPORT_TMP}"
}
trap cleanup EXIT

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
ctest --test-dir "${BUILD_DIR}" --output-on-failure | tee "${WORKDIR}/ctest.txt"
CTEST_RC=${PIPESTATUS[0]}
set -e

# 完整写入临时报告后再 rename；任一步失败都不会留下看似有效的空正式报告。
{
  rcr_write_environment "${WORKDIR}/env.txt" "${BUILD_DIR}" Debug
  cat "${WORKDIR}/env.txt"
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
  cat "${WORKDIR}/ctest.txt"
} >"${REPORT_TMP}"

mv -f "${REPORT_TMP}" "${REPORT}"

echo "evidence: ${REPORT}"
exit "${CTEST_RC}"
