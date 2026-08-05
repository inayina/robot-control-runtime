#!/usr/bin/env bash
# RT2：用 cyclictest 复现 RT1 四个代表条件，并记录缺失诊断工具。
#
# 问题：区分本仓 rcr_benchmark 行为与标准周期唤醒工具看到的内核调度噪声。
# 不改 Runtime；不与 RT1 矩阵同跑 tracing。
#
# 四代表条件（对齐 RT1 smoke 最有信号的格）：
#   1) A76 + performance + OTHER + idle
#   2) A76 + performance + OTHER + same-core stress   ← RT1 异常格
#   3) A76 + performance + FIFO10 + idle
#   4) A76 + performance + FIFO10 + same-core stress  ← 对照
#
# 用法：
#   sudo bash deploy/orangepi/rt2_cyclictest_once.sh
#   # 或：
#   RCR_RT2_ALLOW_DIRTY=1 sudo -E ./linux/scripts/run_realtime_linux_rt2.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
# shellcheck source=lib/cpu_topology.sh
source "${ROOT}/linux/scripts/lib/cpu_topology.sh"
RCR_ROOT="${ROOT}"
cd "${ROOT}"

ALLOW_DIRTY="${RCR_RT2_ALLOW_DIRTY:-0}"
SKIP_GOVERNOR="${RCR_RT2_SKIP_GOVERNOR:-0}"
DURATION_S="${RCR_RT2_DURATION_S:-60}"
INTERVAL_US="${RCR_RT2_INTERVAL_US:-1000}"
FIFO_PRI="${RCR_RT2_FIFO_PRIORITY:-10}"
HIST_US="${RCR_RT2_HIST_US:-10000}"

DIRTY="$(rcr_git_dirty)"
CLASSIFICATION=diagnostic
if [[ "${DIRTY}" == "true" ]]; then
  if [[ "${ALLOW_DIRTY}" != "1" ]]; then
    echo "error: git_dirty=true; commit first or RCR_RT2_ALLOW_DIRTY=1" >&2
    exit 1
  fi
  CLASSIFICATION=experiment
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_ID="${STAMP}_orangepi_rt2_cyclictest"
OUT_ROOT="${ROOT}/evidence/realtime_linux"
FINAL_DIR="${OUT_ROOT}/${RUN_ID}"
if [[ -e "${FINAL_DIR}" ]]; then
  echo "error: refuse overwrite ${FINAL_DIR}" >&2
  exit 1
fi
mkdir -p "${OUT_ROOT}"
TMP_DIR="$(mktemp -d "${OUT_ROOT}/.tmp_${RUN_ID}_XXXXXX")"

declare -A ORIG_GOV=()
GOVERNOR_CHANGED=0

cleanup() {
  restore_governors || true
  if [[ -n "${TMP_DIR}" && -d "${TMP_DIR}" && ! -e "${FINAL_DIR}" ]]; then
    rm -rf "${TMP_DIR}"
  fi
}
trap cleanup EXIT

save_governors() {
  local cpu gov
  for cpu in ${RCR_TOPO_ONLINE//,/ }; do
    gov="$(cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor" 2>/dev/null || true)"
    ORIG_GOV["${cpu}"]="${gov:-unavailable}"
  done
}

set_all_governors() {
  local target="$1" cpu path
  if [[ "${SKIP_GOVERNOR}" == "1" ]]; then
    return 0
  fi
  for cpu in ${RCR_TOPO_ONLINE//,/ }; do
    path="/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor"
    if [[ ! -w "${path}" ]]; then
      echo "error: cannot write ${path}; run as root" >&2
      return 1
    fi
    echo "${target}" >"${path}"
  done
  GOVERNOR_CHANGED=1
}

restore_governors() {
  local cpu path
  [[ "${GOVERNOR_CHANGED}" == "1" ]] || return 0
  for cpu in "${!ORIG_GOV[@]}"; do
    path="/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor"
    if [[ -w "${path}" && "${ORIG_GOV[${cpu}]}" != "unavailable" ]]; then
      echo "${ORIG_GOV[${cpu}]}" >"${path}" || true
    fi
  done
  GOVERNOR_CHANGED=0
}

tool_status() {
  local name="$1"
  if command -v "${name}" >/dev/null 2>&1; then
    echo present
  else
    echo missing
  fi
}

# perf 包装器存在但可能对不上厂商内核。
perf_usable() {
  if ! command -v perf >/dev/null 2>&1; then
    return 1
  fi
  if perf version >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

rcr_topology_write "${TMP_DIR}/topology.txt"
save_governors
set_all_governors performance

CPU="${RCR_TOPO_A76_CPU}"
CT_VER="$(cyclictest --help 2>&1 | head -1 | tr ' ' _ || echo unavailable)"

{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "machine=$(uname -m)"
  echo "os_kernel=$(uname -srm)"
  echo "git_commit=$(rcr_git_commit)"
  echo "git_dirty=${DIRTY}"
  echo "classification=${CLASSIFICATION}"
  echo "platform=orangepi"
  echo "tool=cyclictest"
  echo "tool_version=${CT_VER}"
  echo "governor_requested=performance"
  echo "governor_effective=$(cat /sys/devices/system/cpu/cpu${CPU}/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
  echo "cpu_affinity=${CPU}"
  echo "cpu_class=Cortex-A76_or_highest_freq"
  echo "cpu_topology_summary=${RCR_TOPO_SUMMARY_LINE}"
  echo "duration_s=${DURATION_S}"
  echo "interval_us=${INTERVAL_US}"
  echo "fifo_priority=${FIFO_PRI}"
  echo "temp_c_start=$(rcr_temp_c)"
  echo "stress_ng=$(rcr_stress_ng_status)"
  echo "cyclictest=$(tool_status cyclictest)"
  echo "timerlat=$(tool_status timerlat)"
  echo "osnoise=$(tool_status osnoise)"
  echo "rtla=$(tool_status rtla)"
  echo "trace_cmd=$(tool_status trace-cmd)"
  if perf_usable; then
    echo "perf=present"
  else
    echo "perf=unsupported_kernel_mismatch_or_missing"
  fi
  echo "notes=RT2_cyclictest_four_cells;_not_hard_realtime;_separate_from_rcr_benchmark"
} >"${TMP_DIR}/environment.txt"

{
  echo "script=linux/scripts/run_realtime_linux_rt2.sh"
  echo "duration_s=${DURATION_S}"
  echo "interval_us=${INTERVAL_US}"
  echo "cpu=${CPU}"
  echo "cells=other_idle,other_same_stress,fifo_idle,fifo_same_stress"
} >"${TMP_DIR}/command.txt"

mkdir -p "${TMP_DIR}/cells"
SUMMARY="${TMP_DIR}/summary.txt"
{
  echo "classification=${CLASSIFICATION}"
  echo "rt2_phase=cyclictest_four_representatives"
  echo "duration_s=${DURATION_S}"
  echo "ref_rt1_smoke=20260805T103150Z_orangepi_rt1_smoke"
  echo "notes=compare_to_rcr_benchmark_same_conditions;_not_hard_realtime"
} >"${SUMMARY}"

passed=0
failed=0
unsupported=0
permission_denied=0

parse_cyclictest_histogram_comments() {
  # -q + --histfile 时 Min/Avg/Max 写在 histogram 注释里；配合 -N 单位为 ns。
  local file="$1"
  if [[ ! -f "${file}" ]]; then
    echo "parse_ok=0"
    return 0
  fi
  local total min avg max ovf
  total="$(sed -n 's/^# Total:[[:space:]]*0*\([0-9][0-9]*\).*/\1/p' "${file}" | head -1)"
  min="$(sed -n 's/^# Min Latencies:[[:space:]]*0*\([0-9][0-9]*\).*/\1/p' "${file}" | head -1)"
  avg="$(sed -n 's/^# Avg Latencies:[[:space:]]*0*\([0-9][0-9]*\).*/\1/p' "${file}" | head -1)"
  max="$(sed -n 's/^# Max Latencies:[[:space:]]*0*\([0-9][0-9]*\).*/\1/p' "${file}" | head -1)"
  ovf="$(sed -n 's/^# Histogram Overflows:[[:space:]]*0*\([0-9][0-9]*\).*/\1/p' "${file}" | head -1)"
  if [[ -z "${min}" || -z "${max}" ]]; then
    echo "parse_ok=0"
    return 0
  fi
  echo "parse_ok=1"
  echo "stat_source=histogram_comments"
  echo "histogram_total=${total:-0}"
  echo "histogram_overflows=${ovf:-0}"
  echo "lateness_min_ns=${min}"
  echo "lateness_avg_ns=${avg}"
  echo "lateness_max_ns=${max}"
}

parse_cyclictest_summary() {
  # 兼容无 histfile、stdout 含 Min/Avg/Max 的旧路径。
  local file="$1"
  local line
  line="$(grep -E 'Min:.*Avg:.*Max:' "${file}" | tail -1 || true)"
  if [[ -z "${line}" ]]; then
    echo "parse_ok=0"
    return 0
  fi
  echo "parse_ok=1"
  echo "cyclictest_summary_line=${line}"
  echo "lateness_min=$(echo "${line}" | sed -n 's/.*Min:[[:space:]]*\([0-9][0-9]*\).*/\1/p')"
  echo "lateness_avg=$(echo "${line}" | sed -n 's/.*Avg:[[:space:]]*\([0-9][0-9]*\).*/\1/p')"
  echo "lateness_max=$(echo "${line}" | sed -n 's/.*Max:[[:space:]]*\([0-9][0-9]*\).*/\1/p')"
}

run_cell() {
  local cell_id="$1"
  local policy="$2" # other|fifo
  local load="$3"   # idle|same_stress

  local cell_dir="${TMP_DIR}/cells/${cell_id}"
  mkdir -p "${cell_dir}"
  local result=failed
  local exit_code=0
  local stress_pid=""
  local stress_cmd=none
  local stress_affinity=none

  echo "=== ${cell_id} policy=${policy} load=${load} cpu=${CPU} ===" | tee -a "${TMP_DIR}/stderr.txt"

  if ! command -v cyclictest >/dev/null 2>&1; then
    {
      echo "result=unsupported"
      echo "unsupported_reason=missing_cyclictest"
    } >"${cell_dir}/summary.txt"
    echo "cell ${cell_id} -> unsupported" | tee -a "${SUMMARY}"
    unsupported=$((unsupported + 1))
    return 0
  fi

  if [[ "${load}" == "same_stress" ]]; then
    if ! command -v stress-ng >/dev/null 2>&1; then
      {
        echo "result=unsupported"
        echo "unsupported_reason=missing_stress_ng"
      } >"${cell_dir}/summary.txt"
      echo "cell ${cell_id} -> unsupported" | tee -a "${SUMMARY}"
      unsupported=$((unsupported + 1))
      return 0
    fi
    stress_affinity="${CPU}"
    stress_cmd="stress-ng --cpu $(nproc) --timeout $((DURATION_S + 10))s --taskset ${stress_affinity}"
    stress-ng --cpu "$(nproc)" --timeout "$((DURATION_S + 10))s" --taskset "${stress_affinity}" \
      >"${cell_dir}/stress.log" 2>&1 &
    stress_pid=$!
    sleep 0.5
  fi

  local -a args=(-a "${CPU}" -t 1 -i "${INTERVAL_US}" -D "${DURATION_S}" -m -q -N
    # -N 时直方图单位为 ns；窗过窄会导致 OTHER 几乎全 overflow。用 50ms 窗保留分布。
    -h 50000000 --histfile="${cell_dir}/histogram.txt")
  if [[ "${policy}" == "fifo" ]]; then
    args+=(--policy=fifo -p "${FIFO_PRI}")
  else
    args+=(--policy=other)
  fi

  local cmd="cyclictest ${args[*]}"
  set +e
  cyclictest "${args[@]}" >"${cell_dir}/cyclictest_stdout.txt" 2>"${cell_dir}/cyclictest_stderr.txt"
  exit_code=$?
  set -e

  if [[ -n "${stress_pid}" ]]; then
    kill "${stress_pid}" 2>/dev/null || true
    wait "${stress_pid}" 2>/dev/null || true
  fi

  if grep -qi 'Unable to change scheduling policy' "${cell_dir}/cyclictest_stdout.txt" \
    "${cell_dir}/cyclictest_stderr.txt" 2>/dev/null; then
    result=permission_denied
  elif [[ "${exit_code}" -ne 0 ]]; then
    result=failed
  else
    result=pass
  fi

  {
    echo "result=${result}"
    echo "cell_id=${cell_id}"
    echo "tool=cyclictest"
    echo "command=${cmd}"
    echo "exit_code=${exit_code}"
    echo "cpu_affinity=${CPU}"
    echo "cpu_class=a76"
    echo "governor_requested=performance"
    echo "governor_effective=$(cat /sys/devices/system/cpu/cpu${CPU}/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
    echo "policy_requested=${policy}"
    echo "fifo_priority=${FIFO_PRI}"
    echo "load=${load}"
    echo "stress_command=${stress_cmd}"
    echo "stress_affinity=${stress_affinity}"
    echo "interval_us=${INTERVAL_US}"
    echo "duration_s=${DURATION_S}"
    echo "unit=ns"
    parse_cyclictest_histogram_comments "${cell_dir}/histogram.txt"
    echo "temp_c=$(rcr_temp_c)"
    echo "freq_khz=$(cat /sys/devices/system/cpu/cpu${CPU}/cpufreq/scaling_cur_freq 2>/dev/null || echo unavailable)"
  } >"${cell_dir}/summary.txt"

  echo "cell ${cell_id} -> ${result}" | tee -a "${SUMMARY}"
  case "${result}" in
    pass) passed=$((passed + 1)) ;;
    failed) failed=$((failed + 1)) ;;
    permission_denied) permission_denied=$((permission_denied + 1)) ;;
    unsupported) unsupported=$((unsupported + 1)) ;;
  esac
}

run_cell 01_a76_perf_other_idle other idle
run_cell 02_a76_perf_other_same_stress other same_stress
run_cell 03_a76_perf_fifo_idle fifo idle
run_cell 04_a76_perf_fifo_same_stress fifo same_stress

# 诊断工具可用性记录（本 run 不混开 tracing）
mkdir -p "${TMP_DIR}/diagnostics"
{
  echo "hypothesis=tooling_inventory_only"
  echo "timerlat=$(tool_status timerlat)"
  echo "osnoise=$(tool_status osnoise)"
  echo "rtla=$(tool_status rtla)"
  if perf_usable; then echo "perf=present"; else echo "perf=unsupported"; fi
  echo "trace_cmd=$(tool_status trace-cmd)"
  echo "ftrace_tracers=$(cat /sys/kernel/tracing/available_tracers 2>/dev/null || echo unavailable)"
  echo "result=pass"
  echo "notes=IRQ_vs_thread_split_not_run;_tools_missing_or_kernel_mismatch"
} >"${TMP_DIR}/diagnostics/tooling_inventory.txt"

restore_governors
{
  echo "temp_c_end=$(rcr_temp_c)"
  echo "governor_restored_to=$(rcr_governor)"
} >>"${TMP_DIR}/environment.txt"

{
  echo "cells_pass=${passed}"
  echo "cells_failed=${failed}"
  echo "cells_permission_denied=${permission_denied}"
  echo "cells_unsupported=${unsupported}"
} | tee -a "${SUMMARY}"

(
  cd "${TMP_DIR}"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS
)

mv "${TMP_DIR}" "${FINAL_DIR}"
TMP_DIR=""
trap - EXIT
restore_governors

echo "evidence dir: ${FINAL_DIR}"
echo "classification=${CLASSIFICATION} pass=${passed} failed=${failed} permission_denied=${permission_denied} unsupported=${unsupported}"
if [[ "${failed}" -ne 0 ]]; then
  exit 1
fi
