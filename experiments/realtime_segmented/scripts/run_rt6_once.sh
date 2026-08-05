#!/usr/bin/env bash
# RT6：串行跑 baseline / cb_busy / io_busy，写入 evidence/realtime_linux。
# 不改 Runtime；软件 peer，非 CAN 端到端。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD="${RCR_RT6_BUILD:-${ROOT}/build/rt6}"
OUT_ROOT="${ROOT}/evidence/realtime_linux"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_ID="${STAMP}_rt6_segmented"
FINAL="${OUT_ROOT}/${RUN_ID}"
TICKS="${RCR_RT6_TICKS:-2000}"
PERIOD_US="${RCR_RT6_PERIOD_US:-1000}"
BUSY_US="${RCR_RT6_BUSY_US:-500}"

if [[ ! -x "${BUILD}/rcr_rt6_segments" ]]; then
  echo "error: build first: cmake -S experiments/realtime_segmented -B ${BUILD} -DCMAKE_BUILD_TYPE=Release && cmake --build ${BUILD}" >&2
  exit 1
fi
if [[ -e "${FINAL}" ]]; then
  echo "error: refuse overwrite ${FINAL}" >&2
  exit 1
fi

mkdir -p "${OUT_ROOT}"
TMP="$(mktemp -d "${OUT_ROOT}/.tmp_${RUN_ID}_XXXXXX")"
trap 'rm -rf "${TMP}"' EXIT

{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "os_kernel=$(uname -srm)"
  echo "machine=$(uname -m)"
  echo "git_commit=$(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unavailable)"
  echo "git_dirty=$([[ -n "$(git -C "${ROOT}" status --porcelain 2>/dev/null)" ]] && echo true || echo false)"
  echo "classification=experiment"
  echo "platform=$(uname -m | grep -q aarch64 && echo orangepi_or_arm || echo host)"
  echo "tool=rcr_rt6_segments"
  echo "period_us=${PERIOD_US}"
  echo "ticks=${TICKS}"
  echo "busy_us=${BUSY_US}"
  echo "notes=RT6_segmented_software_peer;_not_CAN_e2e;_not_runtime_merge;_not_hard_realtime"
  echo "temp_c_start=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null | awk '{printf "%.1f", $1/1000}' || echo unavailable)"
} >"${TMP}/environment.txt"

{
  echo "script=experiments/realtime_segmented/scripts/run_rt6_once.sh"
  echo "build=${BUILD}"
} >"${TMP}/command.txt"

mkdir -p "${TMP}/cells"
SUMMARY="${TMP}/summary.txt"
echo "rt6_phase=segmented_software_peer" >"${SUMMARY}"

run_one() {
  local name="$1"
  shift
  local cell="${TMP}/cells/${name}"
  mkdir -p "${cell}"
  set +e
  "$@" >"${cell}/stdout.txt" 2>"${cell}/stderr.txt"
  local rc=$?
  set -e
  local result=failed
  if grep -q '^result=permission_denied' "${cell}/stdout.txt" 2>/dev/null; then
    result=permission_denied
  elif [[ "${rc}" -eq 77 ]]; then
    result=permission_denied
  elif [[ "${rc}" -eq 0 ]] && grep -q '^result=pass' "${cell}/stdout.txt" 2>/dev/null; then
    result=pass
  fi
  {
    echo "result=${result}"
    echo "exit_code=${rc}"
    echo "command=$*"
    cat "${cell}/stdout.txt"
  } >"${cell}/summary.txt"
  echo "cell ${name} -> ${result}" | tee -a "${SUMMARY}"
}

run_one 01_baseline "${BUILD}/rcr_rt6_segments" --mode baseline \
  --ticks "${TICKS}" --period-us "${PERIOD_US}"
run_one 02_cb_busy "${BUILD}/rcr_rt6_segments" --mode cb_busy \
  --ticks "${TICKS}" --period-us "${PERIOD_US}" --busy-us "${BUSY_US}"
run_one 03_io_busy "${BUILD}/rcr_rt6_segments" --mode io_busy \
  --ticks "${TICKS}" --period-us "${PERIOD_US}" --busy-us "${BUSY_US}"
run_one 04_compare_selfcheck "${BUILD}/rcr_rt6_segments" --mode compare \
  --ticks "$(( TICKS < 800 ? TICKS : 800 ))" --period-us "${PERIOD_US}" \
  --busy-us "${BUSY_US}" --self-check

{
  echo "temp_c_end=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null | awk '{printf "%.1f", $1/1000}' || echo unavailable)"
} >>"${TMP}/environment.txt"

(
  cd "${TMP}"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS
)

mv "${TMP}" "${FINAL}"
trap - EXIT
echo "evidence dir: ${FINAL}"
cat "${FINAL}/summary.txt"
