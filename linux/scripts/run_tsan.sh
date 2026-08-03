#!/usr/bin/env bash
# TSan：独立 build 目录。环境 unexpected memory mapping 记为 unsupported，不得写 PASS。
#
# 中间文件放在本次 mktemp -d 目录；正式报告先写同目录 .tmp，字段完整后
# rename。禁止固定 /tmp 路径：第二次运行会因 refuse overwrite 失败，而外层
# 重定向已把正式报告截成 0 字节。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

BUILD_DIR="${RCR_TSAN_BUILD_DIR:-${ROOT}/build/linux-tsan}"
EVIDENCE_DIR="${ROOT}/evidence/sanitizer"
# 秒精度 + PID：同秒连续/并发重跑不得撞名；撞名会 refuse overwrite，不是空报告。
STAMP="$(date -u +%Y%m%dT%H%M%SZ).$$"
REPORT="${EVIDENCE_DIR}/tsan_${STAMP}.txt"
# 与最终报告同目录，保证 rename 在同一文件系统上原子可见。
REPORT_TMP="${EVIDENCE_DIR}/.tsan_${STAMP}.$$.tmp"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rcr_tsan.XXXXXX")"

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
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"

RESULT="failed"
PROBE_OUT=""
PROBE_RC=1
CTEST_OUT=""
CTEST_RC=1

set +e
cmake -S linux -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DRCR_ENABLE_TSAN=ON -DRCR_ENABLE_ASAN=OFF
CMAKE_RC=$?
if [[ "${CMAKE_RC}" -ne 0 ]]; then
  PROBE_OUT="cmake configure failed"
  PROBE_RC="${CMAKE_RC}"
  CTEST_RC="${CMAKE_RC}"
else
  cmake --build "${BUILD_DIR}" -j
  BUILD_RC=$?
  if [[ "${BUILD_RC}" -ne 0 ]]; then
    PROBE_OUT="cmake build failed"
    PROBE_RC="${BUILD_RC}"
    CTEST_RC="${BUILD_RC}"
  else
    PROBE_OUT="$("${BUILD_DIR}/tests/test_mailbox" 2>&1)"
    PROBE_RC=$?
    if echo "${PROBE_OUT}" | grep -qi 'unexpected memory mapping'; then
      RESULT="unsupported"
      CTEST_RC=0
      CTEST_OUT="TSan runtime unsupported in this environment (unexpected memory mapping)"
    elif [[ "${PROBE_RC}" -ne 0 ]]; then
      RESULT="failed"
      CTEST_RC="${PROBE_RC}"
      CTEST_OUT="${PROBE_OUT}"
    else
      CTEST_OUT="$(ctest --test-dir "${BUILD_DIR}" --output-on-failure 2>&1)"
      CTEST_RC=$?
      if [[ "${CTEST_RC}" -eq 0 ]]; then
        RESULT="pass"
      else
        RESULT="failed"
      fi
    fi
  fi
fi
set -e

# 完整写入临时报告后再 rename；任一步失败都不会留下看似有效的空正式报告。
{
  rcr_write_environment "${WORKDIR}/env.txt" "${BUILD_DIR}" Debug
  cat "${WORKDIR}/env.txt"
  echo "sanitizer=TSan"
  echo "tsan_options=${TSAN_OPTIONS}"
  echo "probe_exit_code=${PROBE_RC}"
  echo "ctest_exit_code=${CTEST_RC}"
  echo "result=${RESULT}"
  echo "----- probe -----"
  echo "${PROBE_OUT}"
  echo "----- ctest -----"
  echo "${CTEST_OUT}"
} >"${REPORT_TMP}"

mv -f "${REPORT_TMP}" "${REPORT}"

echo "evidence: ${REPORT} result=${RESULT}"
if [[ "${RESULT}" == "failed" ]]; then
  exit 1
fi
exit 0
