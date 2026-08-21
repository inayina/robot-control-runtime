#!/usr/bin/env bash
# LD5 本机事故演练编排器。
# 只管理本次演练创建的精确子进程和本地 evidence 目录；不操作 host systemd、物理 CAN 或串口。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${RCR_BUILD_DIR:-${ROOT}/build/ld2-qt-off}"
CAN_IFACE="${RCR_LD5_CAN_IFACE:-vcan0}"
OUT_ROOT="${RCR_LD5_OUT_ROOT:-${ROOT}/evidence/ld5_incidents}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="${OUT_ROOT}/${STAMP}"

RCRD="${BUILD_DIR}/rcrd"
SIM="${BUILD_DIR}/rcr_node_sim"
MATRIX="${BUILD_DIR}/rcr_fault_matrix"
BENCH="${BUILD_DIR}/rcr_benchmark"

mkdir -p "${OUT_ROOT}"
if [[ -e "${OUT_DIR}" ]]; then
  echo "error: refuse overwrite ${OUT_DIR}" >&2
  exit 1
fi
mkdir "${OUT_DIR}"

# shellcheck source=lib/evidence_env.sh
source "${ROOT}/linux/scripts/lib/evidence_env.sh"
RCR_ROOT="${ROOT}"
rcr_write_environment "${OUT_DIR}/environment.txt" "${BUILD_DIR}" RelWithDebInfo
cat >>"${OUT_DIR}/environment.txt" <<EOF
classification=LOCAL / VCAN / LOOPBACK / DIRTY
physical_can=NO
physical_rs485=NO
live_systemd_restart=NOT_RUN
notes=LD5 local incident drills; raw evidence is intentionally local and not physical acceptance
EOF

for binary in "${RCRD}" "${SIM}" "${MATRIX}" "${BENCH}"; do
  [[ -x "${binary}" ]] || {
    echo "error: missing executable ${binary}" >&2
    exit 1
  }
done

SIM_PID=""
RCRD_PID=""
STRESS_PID=""
cleanup() {
  for pid in "${RCRD_PID}" "${SIM_PID}" "${STRESS_PID}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
}
trap cleanup EXIT

RESULTS="${OUT_DIR}/RESULTS.txt"
: >"${RESULTS}"

record_result() {
  local name="$1"
  local result="$2"
  local exit_code="$3"
  local detail="$4"
  printf 'scenario=%s result=%s exit_code=%s detail=%s\n' \
    "${name}" "${result}" "${exit_code}" "${detail}" | tee -a "${RESULTS}"
}

run_process_generation() {
  local dir="${OUT_DIR}/01_process_generation"
  mkdir "${dir}"
  printf '%q ' "${SIM}" --can "${CAN_IFACE}" --node-id 1 --heartbeat-ms 40 --duration-ms 2200 \
    >"${dir}/command.txt"
  printf '\n%q ' "${RCRD}" --can "${CAN_IFACE}" --node-id 1 --period-ms 10 \
    >>"${dir}/command.txt"
  printf '\n%q ' "${RCRD}" --can "${CAN_IFACE}" --node-id 1 --period-ms 10 --duration-ms 300 \
    >>"${dir}/command.txt"

  "${SIM}" --can "${CAN_IFACE}" --node-id 1 --heartbeat-ms 40 --duration-ms 2200 \
    >"${dir}/sim.log" 2>&1 &
  SIM_PID=$!
  sleep 0.3
  "${RCRD}" --can "${CAN_IFACE}" --node-id 1 --period-ms 10 \
    >"${dir}/killed_generation.log" 2>&1 &
  RCRD_PID=$!
  sleep 0.3
  local killed_pid="${RCRD_PID}"
  kill -KILL "${killed_pid}"
  local killed_status=0
  wait "${killed_pid}" || killed_status=$?
  RCRD_PID=""

  "${RCRD}" --can "${CAN_IFACE}" --node-id 1 --period-ms 10 --duration-ms 300 \
    >"${dir}/replacement_generation.log" 2>&1 &
  RCRD_PID=$!
  local replacement_pid="${RCRD_PID}"
  local replacement_status=0
  wait "${replacement_pid}" || replacement_status=$?
  RCRD_PID=""
  kill "${SIM_PID}" 2>/dev/null || true
  wait "${SIM_PID}" 2>/dev/null || true
  SIM_PID=""

  {
    echo "killed_pid=${killed_pid}"
    echo "killed_exit_code=${killed_status}"
    echo "replacement_pid=${replacement_pid}"
    echo "replacement_exit_code=${replacement_status}"
    echo "live_systemd_restart=not_run; host unit was not touched"
  } >"${dir}/lifecycle.txt"
  if [[ "${killed_status}" -eq 137 && "${replacement_status}" -eq 0 &&
        "${replacement_pid}" != "${killed_pid}" ]]; then
    record_result process_generation pass 0 "SIGKILL=137; replacement exited 0; distinct pid"
  else
    record_result process_generation failed 1 "expected SIGKILL=137, replacement=0, distinct pid"
  fi
}

run_bad_release_rollback() {
  local dir="${OUT_DIR}/02_bad_release_rollback"
  mkdir "${dir}"
  local status=0
  set +e
  "${ROOT}/deploy/orangepi/test_operations.sh" "${BUILD_DIR}" \
    >"${dir}/stdout.txt" 2>&1
  status=$?
  set -e
  printf '%q %q\n' "${ROOT}/deploy/orangepi/test_operations.sh" "${BUILD_DIR}" >"${dir}/command.txt"
  echo "exit_code=${status}" >"${dir}/exit_code.txt"
  if [[ "${status}" -eq 0 ]]; then
    record_result bad_release_rollback pass 0 "healthy -> version mismatch fail -> rollback"
  else
    record_result bad_release_rollback failed "${status}" "operations contract failed"
  fi
}

run_vcan_faults() {
  local dir="${OUT_DIR}/03_vcan_faults"
  mkdir "${dir}"
  local status=0
  set +e
  "${MATRIX}" --can "${CAN_IFACE}" --sim-path "${SIM}" --rcrd-path "${RCRD}" \
    --evidence "${dir}/fault_matrix.txt" >"${dir}/stdout.txt" 2>&1
  status=$?
  set -e
  printf '%q ' "${MATRIX}" --can "${CAN_IFACE}" --sim-path "${SIM}" --rcrd-path "${RCRD}" \
    --evidence "${dir}/fault_matrix.txt" >"${dir}/command.txt"
  printf '\nexit_code=%s\n' "${status}" >>"${dir}/command.txt"
  if [[ "${status}" -eq 0 ]]; then
    record_result vcan_faults pass 0 "fault matrix covers CommLoss, ACK timeout/hold, restart latch"
  else
    record_result vcan_faults failed "${status}" "fault matrix did not close"
  fi
}

run_modbus_unavailable() {
  local dir="${OUT_DIR}/04_modbus_unavailable"
  mkdir "${dir}"
  local status=0
  set +e
  ctest --test-dir "${BUILD_DIR}" -R '^test_modbus_agent_loopback$' --output-on-failure \
    >"${dir}/stdout.txt" 2>&1
  status=$?
  set -e
  printf 'ctest --test-dir %q -R %q --output-on-failure\n' \
    "${BUILD_DIR}" '^test_modbus_agent_loopback$' >"${dir}/command.txt"
  echo "exit_code=${status}" >"${dir}/exit_code.txt"
  if [[ "${status}" -eq 0 ]]; then
    record_result modbus_unavailable pass 0 "mock loopback passed; unconnected client timeout asserted"
  else
    record_result modbus_unavailable failed "${status}" "Modbus loopback/unavailable contract failed"
  fi
}

run_scheduler_overload() {
  local dir="${OUT_DIR}/05_scheduler_overload"
  mkdir "${dir}"
  local status=0
  local stress_status=0
  local samples="${dir}/samples_lateness_ns.txt"
  : >"${dir}/command.txt"
  printf '%q ' stress-ng --cpu "$(nproc)" --timeout 3s >"${dir}/command.txt"
  printf '\n%q ' "${BENCH}" --duration-ms 1000 --period-us 1000 --callback-delay-us 3000 \
    --samples-out "${samples}" >>"${dir}/command.txt"
  stress-ng --cpu "$(nproc)" --timeout 3s >"${dir}/stress.log" 2>&1 &
  STRESS_PID=$!
  sleep 0.2
  set +e
  "${BENCH}" --duration-ms 1000 --period-us 1000 --callback-delay-us 3000 \
    --samples-out "${samples}" >"${dir}/benchmark_stdout.txt" 2>&1
  status=$?
  set -e
  # 让 stress-ng 自己在 3 秒上限退出，保留其真实退出码；不主动杀掉一个可控的测试进程。
  wait "${STRESS_PID}" 2>/dev/null || stress_status=$?
  STRESS_PID=""
  {
    echo "benchmark_exit_code=${status}"
    echo "stress_exit_code=${stress_status}"
    echo "interpretation=wakeup/deadline observation under ordinary Linux; not CAN latency or hard realtime"
  } >"${dir}/result.txt"
  local misses
  misses="$(sed -n 's/^deadline_misses=//p' "${dir}/benchmark_stdout.txt" | tail -1)"
  if [[ "${status}" -eq 0 && "${stress_status}" -eq 0 && "${misses:-0}" -gt 0 ]]; then
    record_result scheduler_overload pass 0 "deadline_misses=${misses}; bounded stress-ng; controlled callback overrun"
  else
    record_result scheduler_overload failed "${status}" "benchmark=${status}, stress=${stress_status}, misses=${misses:-missing}"
  fi
}

run_process_generation
run_bad_release_rollback
run_vcan_faults
run_modbus_unavailable
run_scheduler_overload

echo "evidence_dir=${OUT_DIR}"
echo "LD5 incidents complete: ${RESULTS}"
if grep -q 'result=failed' "${RESULTS}"; then
  exit 1
fi
