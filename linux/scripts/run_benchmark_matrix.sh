#!/usr/bin/env bash
# 共享 12 格调度/负载矩阵：OTHER/FIFO × idle/stress-ng × 1/5/10 ms。
# 平台 wrapper 只设置输出目录与 platform 标签，不复制循环体。
#
# 必需环境变量：
#   RCR_BENCH_PLATFORM   thinkpad | orangepi | <自定义标签>
#   RCR_BENCH_OUT_ROOT   证据父目录（其下再建时间戳子目录）
# 可选：
#   RCR_BUILD_DIR、RCR_BENCH_DURATION_MS、RCR_BENCH_AFFINITY、RCR_BENCH_FIFO_PRIORITY
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"

PLATFORM="${RCR_BENCH_PLATFORM:?set RCR_BENCH_PLATFORM (thinkpad|orangepi)}"
OUT_ROOT="${RCR_BENCH_OUT_ROOT:?set RCR_BENCH_OUT_ROOT to an evidence parent directory}"
BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/linux}"
BENCH="${BUILD_DIR}/rcr_benchmark"
DURATION_MS="${RCR_BENCH_DURATION_MS:-5000}"
AFFINITY="${RCR_BENCH_AFFINITY:-0}"
FIFO_PRI="${RCR_BENCH_FIFO_PRIORITY:-10}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="${OUT_ROOT}/${STAMP}"

if [[ ! -x "${BENCH}" ]]; then
  echo "error: build rcr_benchmark first (${BENCH})" >&2
  exit 1
fi
if [[ -e "${OUT_DIR}" ]]; then
  echo "error: refuse overwrite ${OUT_DIR}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"
cd "${ROOT}"
rcr_write_environment "${OUT_DIR}/environment.txt" "${BUILD_DIR}" Debug
{
  echo "platform=${PLATFORM}"
  echo "matrix=SCHED_OTHER/FIFO x idle/stress-ng x 1/5/10ms"
  echo "notes=wakeup lateness only; not CAN/control latency; not hard realtime"
} >>"${OUT_DIR}/environment.txt"

SUMMARY="${OUT_DIR}/SUMMARY.txt"
passed=0
failed=0
permission_denied=0
unsupported=0
{
  echo "platform=${PLATFORM}"
  echo "matrix=SCHED_OTHER/FIFO x idle/stress-ng x 1/5/10ms"
  echo "duration_ms=${DURATION_MS}"
  echo "cpu_affinity_requested=${AFFINITY}"
  echo "fifo_priority=${FIFO_PRI}"
  echo "notes=wakeup lateness only; not CAN/control latency; not hard realtime"
} >"${SUMMARY}"

run_one() {
  local policy="$1" # other|fifo
  local load="$2"   # idle|stress
  local period_us="$3"
  local cell_dir="${OUT_DIR}/${policy}_${load}_${period_us}us"
  mkdir -p "${cell_dir}"
  local samples="${cell_dir}/samples_lateness_ns.txt"
  local summary="${cell_dir}/summary.txt"
  local stress_pid=""
  local stress_cmd="none"
  local result="failed"
  local exit_code=0

  if [[ "${load}" == "stress" ]]; then
    if ! command -v stress-ng >/dev/null 2>&1; then
      {
        echo "result=unsupported"
        echo "command=missing stress-ng"
        echo "exit_code=0"
        echo "policy_requested=${policy}"
        echo "load=stress-ng"
        echo "period_us=${period_us}"
        echo "detail=stress-ng not installed"
      } >"${summary}"
      echo "cell ${policy}/${load}/${period_us}us -> unsupported" | tee -a "${SUMMARY}"
      unsupported=$((unsupported + 1))
      return 0
    fi
    stress_cmd="stress-ng --cpu $(nproc) --timeout $((DURATION_MS / 1000 + 2))s --taskset ${AFFINITY}"
    # shellcheck disable=SC2086
    stress-ng --cpu "$(nproc)" --timeout "$((DURATION_MS / 1000 + 2))s" --taskset "${AFFINITY}" \
      >"${cell_dir}/stress.log" 2>&1 &
    stress_pid=$!
    sleep 0.5
    if ! kill -0 "${stress_pid}" 2>/dev/null; then
      local stress_status=0
      wait "${stress_pid}" || stress_status=$?
      {
        echo "result=failed"
        echo "command=${stress_cmd}"
        echo "exit_code=${stress_status}"
        echo "policy_requested=${policy}"
        echo "load=stress-ng"
        echo "period_us=${period_us}"
        echo "detail=stress-ng exited before benchmark started"
      } >"${summary}"
      echo "cell ${policy}/${load}/${period_us}us -> failed (stress-ng startup)" | tee -a "${SUMMARY}"
      failed=$((failed + 1))
      return 0
    fi
  fi

  local args=(--duration-ms "${DURATION_MS}" --period-us "${period_us}"
    --cpu-affinity "${AFFINITY}" --samples-out "${samples}")
  if [[ "${policy}" == "fifo" ]]; then
    args+=(--fifo-priority "${FIFO_PRI}" --require-fifo)
  fi

  set +e
  "${BENCH}" "${args[@]}" 2>&1 | tee "${cell_dir}/bench_stdout.txt"
  exit_code=$?
  set -e

  if [[ -n "${stress_pid}" ]]; then
    kill "${stress_pid}" 2>/dev/null || true
    wait "${stress_pid}" 2>/dev/null || true
  fi

  local fifo_enabled
  local fifo_error
  local affinity_enabled
  local affinity_error
  fifo_enabled="$(grep -E '^fifo_enabled=' "${cell_dir}/bench_stdout.txt" | cut -d= -f2 || echo 0)"
  fifo_error="$(grep -E '^fifo_error=' "${cell_dir}/bench_stdout.txt" | cut -d= -f2 || echo 0)"
  affinity_enabled="$(grep -E '^affinity_enabled=' "${cell_dir}/bench_stdout.txt" | cut -d= -f2 || echo 0)"
  affinity_error="$(grep -E '^affinity_error=' "${cell_dir}/bench_stdout.txt" | cut -d= -f2 || echo 0)"
  if [[ "${exit_code}" -ne 0 ]]; then
    if [[ "${fifo_error}" == "1" || "${fifo_error}" == "13" ||
          "${affinity_error}" == "1" || "${affinity_error}" == "13" ]]; then
      result="permission_denied"
    else
      result="failed"
    fi
  elif [[ "${policy}" == "fifo" && "${fifo_enabled}" != "1" ]]; then
    if [[ "${fifo_error}" == "1" || "${fifo_error}" == "13" ]]; then
      result="permission_denied"
    else
      result="failed"
    fi
  elif [[ "${AFFINITY}" -ge 0 && "${affinity_enabled}" != "1" ]]; then
    if [[ "${affinity_error}" == "1" || "${affinity_error}" == "13" ]]; then
      result="permission_denied"
    else
      result="failed"
    fi
  elif [[ "${exit_code}" -eq 0 ]]; then
    result="pass"
  else
    result="failed"
  fi

  {
    echo "result=${result}"
    echo "platform=${PLATFORM}"
    echo "command=${BENCH} ${args[*]}"
    echo "exit_code=${exit_code}"
    echo "policy_requested=${policy}"
    echo "fifo_priority_requested=${FIFO_PRI}"
    echo "load=${load}"
    echo "stress_command=${stress_cmd}"
    echo "cpu_affinity_requested=${AFFINITY}"
    echo "period_us=${period_us}"
    echo "duration_ms=${DURATION_MS}"
    cat "${cell_dir}/bench_stdout.txt"
  } >"${summary}"

  echo "cell ${policy}/${load}/${period_us}us -> ${result}" | tee -a "${SUMMARY}"
  case "${result}" in
    pass) passed=$((passed + 1)) ;;
    failed) failed=$((failed + 1)) ;;
    permission_denied) permission_denied=$((permission_denied + 1)) ;;
  esac
}

# 固定顺序：普通空载 → 普通压力 → FIFO 空载 → FIFO 压力
for period in 1000 5000 10000; do
  run_one other idle "${period}"
done
for period in 1000 5000 10000; do
  run_one other stress "${period}"
done
for period in 1000 5000 10000; do
  run_one fifo idle "${period}"
done
for period in 1000 5000 10000; do
  run_one fifo stress "${period}"
done

{
  echo "cells_pass=${passed}"
  echo "cells_failed=${failed}"
  echo "cells_permission_denied=${permission_denied}"
  echo "cells_unsupported=${unsupported}"
} | tee -a "${SUMMARY}"
echo "evidence dir: ${OUT_DIR}"
echo "See SUMMARY.txt. Not hard realtime; platform=${PLATFORM}."
if [[ "${failed}" -ne 0 ]]; then
  exit 1
fi
