#!/usr/bin/env bash
# RT3：串行跑内存 / PI 锁 / 周期路径三类夹具，写入 evidence/realtime_linux。
# 不改 Runtime；缺 FIFO/mlock 权限记 visible 结果。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD="${RCR_RT3_BUILD:-${ROOT}/build/rt3}"
OUT_ROOT="${ROOT}/evidence/realtime_linux"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_ID="${STAMP}_rt3_userspace"
FINAL="${OUT_ROOT}/${RUN_ID}"

if [[ ! -x "${BUILD}/rcr_rt3_mlock" ]]; then
  echo "error: build first: cmake -S experiments/realtime_userspace -B ${BUILD} && cmake --build ${BUILD}" >&2
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
  echo "tool=rcr_rt3_userspace_fixtures"
  echo "notes=RT3_memory_lock_cycle;_not_runtime_merge;_not_hard_realtime"
} >"${TMP}/environment.txt"

{
  echo "script=experiments/realtime_userspace/scripts/run_rt3_once.sh"
  echo "build=${BUILD}"
} >"${TMP}/command.txt"

mkdir -p "${TMP}/cells"
SUMMARY="${TMP}/summary.txt"
echo "rt3_phase=userspace_three_classes" >"${SUMMARY}"

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
  elif grep -q '^result=unsupported' "${cell}/stdout.txt" 2>/dev/null; then
    result=unsupported
  elif [[ "${rc}" -eq 0 ]] && grep -q '^result=pass' "${cell}/stdout.txt" 2>/dev/null; then
    result=pass
  elif [[ "${rc}" -eq 77 ]]; then
    result=unsupported
  fi
  {
    echo "result=${result}"
    echo "exit_code=${rc}"
    echo "command=$*"
    cat "${cell}/stdout.txt"
  } >"${cell}/summary.txt"
  echo "cell ${name} -> ${result}" | tee -a "${SUMMARY}"
}

run_one 01_mlock "${BUILD}/rcr_rt3_mlock" --bytes 16777216
run_one 02_pi_on "${BUILD}/rcr_rt3_pi_mutex" --work-ms 40 --pi
run_one 03_pi_off "${BUILD}/rcr_rt3_pi_mutex" --work-ms 40 --no-pi
run_one 04_cycle_compare "${BUILD}/rcr_rt3_cycle_path" --mode compare --ticks 2000 --period-us 1000
run_one 05_cycle_busy "${BUILD}/rcr_rt3_cycle_path" --mode busy --busy-us 3000 --ticks 200 --period-us 1000

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
