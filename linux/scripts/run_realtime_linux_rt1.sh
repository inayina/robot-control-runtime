#!/usr/bin/env bash
# RT1：Orange Pi 普通内核 10 格最小矩阵（独立于 P2 的 12 格部署对照）。
#
# 问题：需要可复现的周期唤醒基线，并区分大小核、同核/异核压力与 DVFS。
# 不改 Runtime；不扩展 run_benchmark_matrix.sh，避免把部署 12 格与实时学习矩阵缠在一起。
#
# 用法（在仓库根、优选板上）：
#   RCR_RT1_MODE=smoke ./linux/scripts/run_realtime_linux_rt1.sh
#   RCR_RT1_MODE=formal ./linux/scripts/run_realtime_linux_rt1.sh
#
# 环境变量：
#   RCR_RT1_MODE            smoke|formal（必需）
#   RCR_BUILD_DIR           默认 build/linux
#   RCR_RT1_DURATION_MS     覆盖默认时长（smoke=60000，formal=1800000）
#   RCR_RT1_FIFO_PRIORITY   默认 10
#   RCR_RT1_ALLOW_DIRTY=1   允许 dirty tree（只能标 classification=experiment）
#   RCR_RT1_SKIP_GOVERNOR=1 不改 governor（默认：请求与实际不符的格记 unsupported）
#   RCR_RT1_IGNORE_GOVERNOR_MISMATCH=1  与 SKIP 联用：仍跑格但 stamp governor_contract=relaxed
#                                      （只能用于机制自检，不能当 baseline）
#   RCR_RT1_REQUIRE_FIFO=1  默认 1；FIFO 格要求生效
#   RCR_RT1_CROSS_5MS=1     smoke/formal 额外跑两个 5ms 交叉格（默认 formal=1 smoke=0）
#
# FIFO 与改 governor 通常需要 root；见 deploy/orangepi/rt1_smoke_once.sh。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
# shellcheck source=lib/cpu_topology.sh
source "${ROOT}/linux/scripts/lib/cpu_topology.sh"
RCR_ROOT="${ROOT}"
cd "${ROOT}"

MODE="${RCR_RT1_MODE:-}"
if [[ "${MODE}" != "smoke" && "${MODE}" != "formal" ]]; then
  echo "error: set RCR_RT1_MODE=smoke|formal" >&2
  exit 1
fi

BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/linux}"
BENCH="${BUILD_DIR}/rcr_benchmark"
FIFO_PRI="${RCR_RT1_FIFO_PRIORITY:-10}"
ALLOW_DIRTY="${RCR_RT1_ALLOW_DIRTY:-0}"
SKIP_GOVERNOR="${RCR_RT1_SKIP_GOVERNOR:-0}"
IGNORE_GOV_MISMATCH="${RCR_RT1_IGNORE_GOVERNOR_MISMATCH:-0}"
REQUIRE_FIFO="${RCR_RT1_REQUIRE_FIFO:-1}"
if [[ -n "${RCR_RT1_CROSS_5MS:-}" ]]; then
  CROSS_5MS="${RCR_RT1_CROSS_5MS}"
elif [[ "${MODE}" == "formal" ]]; then
  CROSS_5MS=1
else
  CROSS_5MS=0
fi

if [[ -n "${RCR_RT1_DURATION_MS:-}" ]]; then
  DURATION_MS="${RCR_RT1_DURATION_MS}"
elif [[ "${MODE}" == "smoke" ]]; then
  DURATION_MS=60000
else
  DURATION_MS=1800000
fi

DIRTY="$(rcr_git_dirty)"
CLASSIFICATION=baseline
if [[ "${DIRTY}" == "true" ]]; then
  if [[ "${ALLOW_DIRTY}" != "1" ]]; then
    echo "error: git_dirty=true; commit first or set RCR_RT1_ALLOW_DIRTY=1 (marks experiment)" >&2
    exit 1
  fi
  CLASSIFICATION=experiment
fi
if [[ "${MODE}" == "smoke" && "${CLASSIFICATION}" == "baseline" ]]; then
  CLASSIFICATION=baseline
fi
# smoke 即使 clean 也标 smoke 子类，避免与 30min baseline 混比。
if [[ "${MODE}" == "smoke" ]]; then
  CLASSIFICATION=smoke
fi
if [[ "${IGNORE_GOV_MISMATCH}" == "1" || "${SKIP_GOVERNOR}" == "1" ]]; then
  # 未强制 governor 合同的结果只能当机制/实验观察。
  if [[ "${CLASSIFICATION}" == "baseline" || "${CLASSIFICATION}" == "smoke" ]]; then
    CLASSIFICATION=experiment
  fi
fi

if [[ "${MODE}" == "formal" ]]; then
  if [[ "${SKIP_GOVERNOR}" == "1" || "${IGNORE_GOV_MISMATCH}" == "1" ]]; then
    echo "error: formal baseline refuses SKIP_GOVERNOR / IGNORE_GOVERNOR_MISMATCH" >&2
    exit 1
  fi
fi

if [[ ! -x "${BENCH}" ]]; then
  echo "error: build Release rcr_benchmark first (${BENCH})" >&2
  exit 1
fi

DETECTED_BUILD_TYPE=unavailable
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  DETECTED_BUILD_TYPE="$(awk -F= '/^CMAKE_BUILD_TYPE:/{print $2; exit}' "${BUILD_DIR}/CMakeCache.txt" || echo unavailable)"
fi
if [[ "${MODE}" == "formal" && "${DETECTED_BUILD_TYPE}" != "Release" ]]; then
  echo "error: formal requires CMAKE_BUILD_TYPE=Release (found ${DETECTED_BUILD_TYPE})" >&2
  exit 1
fi
if [[ "${MODE}" == "smoke" && "${DETECTED_BUILD_TYPE}" != "Release" && "${ALLOW_DIRTY}" != "1" ]]; then
  echo "error: smoke baseline requires Release build (found ${DETECTED_BUILD_TYPE}); or ALLOW_DIRTY=1 for experiment" >&2
  exit 1
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
RUN_ID="${STAMP}_orangepi_rt1_${MODE}"
OUT_ROOT="${ROOT}/evidence/realtime_linux"
FINAL_DIR="${OUT_ROOT}/${RUN_ID}"
if [[ -e "${FINAL_DIR}" ]]; then
  echo "error: refuse overwrite ${FINAL_DIR}" >&2
  exit 1
fi

mkdir -p "${OUT_ROOT}"
TMP_DIR="$(mktemp -d "${OUT_ROOT}/.tmp_${RUN_ID}_XXXXXX")"
cleanup_tmp() {
  if [[ -n "${TMP_DIR}" && -d "${TMP_DIR}" && ! -e "${FINAL_DIR}" ]]; then
    rm -rf "${TMP_DIR}"
  fi
}

# ---- governor 保存/恢复：只在实验窗口改，失败可见 ----
declare -A ORIG_GOV=()
GOVERNOR_CHANGED=0

save_governors() {
  local cpu gov
  for cpu in ${RCR_TOPO_ONLINE//,/ }; do
    gov="$(cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor" 2>/dev/null || true)"
    ORIG_GOV["${cpu}"]="${gov:-unavailable}"
  done
}

set_all_governors() {
  local target="$1"
  local cpu path
  if [[ "${SKIP_GOVERNOR}" == "1" ]]; then
    return 0
  fi
  for cpu in ${RCR_TOPO_ONLINE//,/ }; do
    path="/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor"
    if [[ ! -w "${path}" ]]; then
      echo "error: cannot write ${path}; run as root or RCR_RT1_SKIP_GOVERNOR=1" >&2
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
trap 'restore_governors; cleanup_tmp' EXIT

rcr_topology_write "${TMP_DIR}/topology.txt"
save_governors

TOOL_VER="rcr_benchmark"
STRESS_VER="$(stress-ng --version 2>/dev/null | head -1 | tr ' ' _ || echo missing)"

{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname)"
  echo "machine=$(uname -m)"
  echo "cpu_model=$(rcr_cpu_model)"
  echo "os_kernel=$(uname -srm)"
  echo "compiler=$(${CXX:-c++} --version | head -1)"
  echo "build_type=${DETECTED_BUILD_TYPE}"
  echo "build_dir=${BUILD_DIR}"
  echo "git_commit=$(rcr_git_commit)"
  echo "git_dirty=${DIRTY}"
  echo "governor=$(rcr_governor)"
  echo "temp_c_start=$(rcr_temp_c)"
  echo "stress_ng=$(rcr_stress_ng_status)"
  echo "classification=${CLASSIFICATION}"
  echo "platform=orangepi"
  echo "tool=rcr_benchmark"
  echo "tool_version=${TOOL_VER}"
  echo "stress_ng_version=${STRESS_VER}"
  echo "rt1_mode=${MODE}"
  echo "duration_ms=${DURATION_MS}"
  echo "fifo_priority=${FIFO_PRI}"
  echo "cpu_topology_summary=${RCR_TOPO_SUMMARY_LINE}"
  echo "cpu_affinity_a76=${RCR_TOPO_A76_CPU}"
  echo "cpu_affinity_a55=${RCR_TOPO_A55_CPU}"
  echo "cpu_class_a76=Cortex-A76_or_highest_freq"
  echo "cpu_class_a55=Cortex-A55_or_lower_freq"
  echo "period_us_primary=1000"
  echo "cross_5ms=${CROSS_5MS}"
  echo "callback_delay_us=0"
  echo "kernel_config_hash=unavailable"
  echo "unsupported_reason=none"
  echo "notes=wakeup_lateness_only;_not_CAN_or_control_latency;_not_hard_realtime;_ordinary_kernel"
} >"${TMP_DIR}/environment.txt"

{
  echo "script=linux/scripts/run_realtime_linux_rt1.sh"
  echo "mode=${MODE}"
  echo "duration_ms=${DURATION_MS}"
  echo "bench=${BENCH}"
  echo "fifo_priority=${FIFO_PRI}"
  echo "cross_5ms=${CROSS_5MS}"
  echo "allow_dirty=${ALLOW_DIRTY}"
  echo "skip_governor=${SKIP_GOVERNOR}"
} >"${TMP_DIR}/command.txt"

mkdir -p "${TMP_DIR}/cells"
SUMMARY="${TMP_DIR}/summary.txt"
{
  echo "classification=${CLASSIFICATION}"
  echo "rt1_mode=${MODE}"
  echo "duration_ms=${DURATION_MS}"
  echo "matrix=rt1_10_cells_plus_optional_5ms_cross"
  echo "notes=wakeup lateness only; not hard realtime"
} >"${SUMMARY}"

passed=0
failed=0
permission_denied=0
unsupported=0

read_kv() {
  local file="$1" key="$2" default="${3:-}"
  local line
  line="$(grep -E "^${key}=" "${file}" 2>/dev/null | tail -1 || true)"
  if [[ -z "${line}" ]]; then
    echo "${default}"
  else
    echo "${line#*=}"
  fi
}

freq_of_cpu() {
  cat "/sys/devices/system/cpu/cpu$1/cpufreq/scaling_cur_freq" 2>/dev/null || echo unavailable
}

run_cell() {
  local cell_id="$1"
  local cpu_class="$2"   # a76|a55
  local gov_target="$3"  # performance|ondemand
  local policy="$4"      # other|fifo
  local load="$5"        # idle|other_stress|same_stress
  local period_us="$6"

  local affinity
  if [[ "${cpu_class}" == "a76" ]]; then
    affinity="${RCR_TOPO_A76_CPU}"
  else
    affinity="${RCR_TOPO_A55_CPU}"
  fi

  local cell_dir="${TMP_DIR}/cells/${cell_id}"
  mkdir -p "${cell_dir}"
  local samples="${cell_dir}/samples.txt"
  local cell_summary="${cell_dir}/summary.txt"
  local result=failed
  local exit_code=0
  local stress_pid=""
  local stress_cmd=none
  local stress_affinity=none
  local unsupported_reason=none

  echo "=== cell ${cell_id} cpu=${affinity}(${cpu_class}) gov=${gov_target} policy=${policy} load=${load} period_us=${period_us} ===" \
    | tee -a "${TMP_DIR}/stderr.txt"

  if ! set_all_governors "${gov_target}"; then
    {
      echo "result=failed"
      echo "detail=governor_set_failed"
      echo "governor_requested=${gov_target}"
    } >"${cell_summary}"
    echo "cell ${cell_id} -> failed (governor)" | tee -a "${SUMMARY}"
    failed=$((failed + 1))
    return 0
  fi

  local gov_effective
  gov_effective="$(cat "/sys/devices/system/cpu/cpu${affinity}/cpufreq/scaling_governor" 2>/dev/null || echo unavailable)"
  if [[ "${SKIP_GOVERNOR}" == "1" && "${gov_effective}" != "${gov_target}" ]]; then
    if [[ "${IGNORE_GOV_MISMATCH}" != "1" ]]; then
      {
        echo "result=unsupported"
        echo "unsupported_reason=governor_skip_mismatch"
        echo "governor_requested=${gov_target}"
        echo "governor_effective=${gov_effective}"
        echo "cpu_affinity=${affinity}"
        echo "cpu_class=${cpu_class}"
        echo "policy_requested=${policy}"
        echo "load=${load}"
        echo "period_us=${period_us}"
      } >"${cell_summary}"
      echo "cell ${cell_id} -> unsupported (governor_skip_mismatch)" | tee -a "${SUMMARY}"
      unsupported=$((unsupported + 1))
      return 0
    fi
    # 机制自检：继续跑，但不得把 performance 合同当成已生效。
    echo "warning: governor_contract=relaxed requested=${gov_target} effective=${gov_effective}" \
      | tee -a "${TMP_DIR}/stderr.txt"
  fi
  local freq_start
  freq_start="$(freq_of_cpu "${affinity}")"
  local temp_start
  temp_start="$(rcr_temp_c)"

  if [[ "${load}" != "idle" ]]; then
    if ! command -v stress-ng >/dev/null 2>&1; then
      unsupported_reason=missing_stress_ng
      {
        echo "result=unsupported"
        echo "unsupported_reason=${unsupported_reason}"
        echo "governor_requested=${gov_target}"
        echo "governor_effective=${gov_effective}"
        echo "cpu_affinity=${affinity}"
        echo "cpu_class=${cpu_class}"
        echo "policy_requested=${policy}"
        echo "load=${load}"
        echo "period_us=${period_us}"
      } >"${cell_summary}"
      echo "cell ${cell_id} -> unsupported (${unsupported_reason})" | tee -a "${SUMMARY}"
      unsupported=$((unsupported + 1))
      return 0
    fi
    if [[ "${load}" == "same_stress" ]]; then
      stress_affinity="${affinity}"
      # 多个 worker 挤在同一核，放大同核竞争；与 pilot 同构。
      local n_workers
      n_workers="$(nproc)"
      stress_cmd="stress-ng --cpu ${n_workers} --timeout $((DURATION_MS / 1000 + 5))s --taskset ${stress_affinity}"
      stress-ng --cpu "${n_workers}" --timeout "$((DURATION_MS / 1000 + 5))s" --taskset "${stress_affinity}" \
        >"${cell_dir}/stress.log" 2>&1 &
    else
      stress_affinity="$(rcr_topology_others "${affinity}")"
      local n_others
      n_others="$(awk -F, '{print NF}' <<<"${stress_affinity}")"
      stress_cmd="stress-ng --cpu ${n_others} --timeout $((DURATION_MS / 1000 + 5))s --taskset ${stress_affinity}"
      stress-ng --cpu "${n_others}" --timeout "$((DURATION_MS / 1000 + 5))s" --taskset "${stress_affinity}" \
        >"${cell_dir}/stress.log" 2>&1 &
    fi
    stress_pid=$!
    sleep 0.5
    if ! kill -0 "${stress_pid}" 2>/dev/null; then
      local st=0
      wait "${stress_pid}" || st=$?
      {
        echo "result=failed"
        echo "detail=stress_ng_startup_failed"
        echo "exit_code=${st}"
        echo "stress_command=${stress_cmd}"
      } >"${cell_summary}"
      echo "cell ${cell_id} -> failed (stress startup)" | tee -a "${SUMMARY}"
      failed=$((failed + 1))
      return 0
    fi
  fi

  local args=(--duration-ms "${DURATION_MS}" --period-us "${period_us}"
    --cpu-affinity "${affinity}" --samples-out "${samples}")
  if [[ "${policy}" == "fifo" ]]; then
    args+=(--fifo-priority "${FIFO_PRI}")
    if [[ "${REQUIRE_FIFO}" == "1" ]]; then
      args+=(--require-fifo)
    fi
  fi

  set +e
  "${BENCH}" "${args[@]}" >"${cell_dir}/bench_stdout.txt" 2>"${cell_dir}/bench_stderr.txt"
  exit_code=$?
  set -e

  if [[ -n "${stress_pid}" ]]; then
    kill "${stress_pid}" 2>/dev/null || true
    wait "${stress_pid}" 2>/dev/null || true
  fi

  local freq_end temp_end
  freq_end="$(freq_of_cpu "${affinity}")"
  temp_end="$(rcr_temp_c)"

  local fifo_enabled affinity_enabled fifo_error affinity_error
  fifo_enabled="$(read_kv "${cell_dir}/bench_stdout.txt" fifo_enabled 0)"
  affinity_enabled="$(read_kv "${cell_dir}/bench_stdout.txt" affinity_enabled 0)"
  fifo_error="$(read_kv "${cell_dir}/bench_stdout.txt" fifo_error 0)"
  affinity_error="$(read_kv "${cell_dir}/bench_stdout.txt" affinity_error 0)"

  if [[ "${exit_code}" -ne 0 ]]; then
    if [[ "${fifo_error}" == "1" || "${fifo_error}" == "13" ||
          "${affinity_error}" == "1" || "${affinity_error}" == "13" ]]; then
      result=permission_denied
    else
      result=failed
    fi
  elif [[ "${policy}" == "fifo" && "${fifo_enabled}" != "1" ]]; then
    if [[ "${fifo_error}" == "1" || "${fifo_error}" == "13" ]]; then
      result=permission_denied
    else
      result=failed
    fi
  elif [[ "${affinity_enabled}" != "1" ]]; then
    if [[ "${affinity_error}" == "1" || "${affinity_error}" == "13" ]]; then
      result=permission_denied
    else
      result=failed
    fi
  else
    result=pass
  fi

  {
    echo "result=${result}"
    echo "cell_id=${cell_id}"
    echo "command=${BENCH} ${args[*]}"
    echo "exit_code=${exit_code}"
    echo "cpu_affinity=${affinity}"
    echo "cpu_class=${cpu_class}"
    echo "governor_requested=${gov_target}"
    echo "governor_effective=${gov_effective}"
    if [[ "${SKIP_GOVERNOR}" == "1" && "${gov_effective}" != "${gov_target}" && "${IGNORE_GOV_MISMATCH}" == "1" ]]; then
      echo "governor_contract=relaxed"
    else
      echo "governor_contract=enforced"
    fi
    echo "policy_requested=${policy}"
    echo "fifo_priority=${FIFO_PRI}"
    echo "load=${load}"
    echo "stress_command=${stress_cmd}"
    echo "stress_affinity=${stress_affinity}"
    echo "period_us=${period_us}"
    echo "duration_ms=${DURATION_MS}"
    echo "freq_start_khz=${freq_start}"
    echo "freq_end_khz=${freq_end}"
    echo "temp_c_start=${temp_start}"
    echo "temp_c_end=${temp_end}"
    echo "unsupported_reason=${unsupported_reason}"
    cat "${cell_dir}/bench_stdout.txt"
  } >"${cell_summary}"

  echo "cell ${cell_id} -> ${result}" | tee -a "${SUMMARY}"
  case "${result}" in
    pass) passed=$((passed + 1)) ;;
    failed) failed=$((failed + 1)) ;;
    permission_denied) permission_denied=$((permission_denied + 1)) ;;
    unsupported) unsupported=$((unsupported + 1)) ;;
  esac
}

# 10 格主矩阵（1 ms）
run_cell 01_a76_perf_other_idle           a76 performance other idle          1000
run_cell 02_a76_perf_other_other_stress   a76 performance other other_stress  1000
run_cell 03_a76_perf_other_same_stress    a76 performance other same_stress   1000
run_cell 04_a76_perf_fifo_idle            a76 performance fifo  idle          1000
run_cell 05_a76_perf_fifo_other_stress    a76 performance fifo  other_stress  1000
run_cell 06_a76_perf_fifo_same_stress     a76 performance fifo  same_stress   1000
run_cell 07_a55_perf_other_other_stress   a55 performance other other_stress  1000
run_cell 08_a55_perf_fifo_other_stress    a55 performance fifo  other_stress  1000
run_cell 09_a76_ondemand_other_other_stress a76 ondemand other other_stress 1000
run_cell 10_a76_ondemand_fifo_other_stress  a76 ondemand fifo  other_stress 1000

# 5 ms 交叉：OTHER+other_stress 与 FIFO+other_stress（A76 + performance）
if [[ "${CROSS_5MS}" == "1" ]]; then
  run_cell 11_a76_perf_other_other_stress_5ms a76 performance other other_stress 5000
  run_cell 12_a76_perf_fifo_other_stress_5ms  a76 performance fifo  other_stress 5000
fi

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

# stderr 汇总
{
  echo "matrix_complete=1"
  echo "see_cells_for_per_cell_stderr=1"
} >>"${TMP_DIR}/stderr.txt"

(
  cd "${TMP_DIR}"
  find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum >SHA256SUMS
)

mkdir -p "${OUT_ROOT}"
mv "${TMP_DIR}" "${FINAL_DIR}"
TMP_DIR=""  # 避免 EXIT 清掉已发布目录
trap - EXIT
restore_governors

echo "evidence dir: ${FINAL_DIR}"
echo "classification=${CLASSIFICATION} mode=${MODE} pass=${passed} failed=${failed} permission_denied=${permission_denied} unsupported=${unsupported}"
echo "Not hard realtime. Review smoke before formal 30min matrix."
if [[ "${failed}" -ne 0 ]]; then
  exit 1
fi
