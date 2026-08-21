#!/usr/bin/env bash
# LD2 Operations Plane：只管理 release/current、systemd 观察和证据收集。
# 不打开 SocketCAN/串口，不发送 Runtime 命令，不改变 CAN/Modbus 状态。
set -euo pipefail

PREFIX="/opt/robot-control-runtime"
OUTPUT=""
SERVICE="rcr-cell-app.service"
CELL_HOST="127.0.0.1"
CELL_PORT="5750"
EXPECTED_RELEASE=""
RESTART=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
INSTALLER="${ROOT}/deploy/orangepi/install_release.sh"
ROLLBACK="${ROOT}/deploy/orangepi/rollback_release.sh"
PROBE="${ROOT}/deploy/orangepi/cel1_status_probe.py"
OBSERVE="${ROOT}/deploy/orangepi/rcr_observe.py"
if [[ -f "${SCRIPT_DIR}/cel1_status_probe.py" ]]; then
  # 安装后的 operations CLI 与其只读 probe 同目录；升级仍只在源码树执行。
  PROBE="${SCRIPT_DIR}/cel1_status_probe.py"
  OBSERVE="${SCRIPT_DIR}/rcr_observe.py"
  ROLLBACK="${SCRIPT_DIR}/rollback_release.sh"
fi

usage() {
  cat <<'EOF'
usage: rcr_operations.sh <status|observe|healthcheck|collect-logs|deploy|upgrade|rollback> [options]

Common options:
  --prefix PATH             release root (default: /opt/robot-control-runtime)
  --service UNIT            exact systemd unit (default: rcr-cell-app.service)
  --cell-host HOST          CEL1 read-only probe host (default: 127.0.0.1)
  --cell-port PORT          CEL1 read-only probe port (default: 5750)
  --expected-release ID     require current/<ID>
  --output DIR              incident bundle destination
  --restart                 try-restart the exact selected service after switch
  --build-dir PATH          build directory for deploy/upgrade
  --release-id ID           release id passed to install_release.sh
  --rollback-to ID          explicit existing release used after failed upgrade

healthcheck is read-only.  It reports UNKNOWN/UNAVAILABLE when a source does
not exist; process_alive never implies Runtime or device health.
EOF
}

die() { echo "error: $*" >&2; exit 1; }
is_abs() { [[ "$1" == /* ]]; }
unit_exists() { command -v systemctl >/dev/null 2>&1; }

manifest_value() {
  local key="$1" file="$2"
  [[ -f "${file}" ]] || return 0
  awk -F= -v wanted="${key}" '$1 == wanted { print substr($0, index($0, "=") + 1); exit }' "${file}"
}

current_release_id() {
  [[ -L "${PREFIX}/current" ]] || return 0
  local target
  target="$(readlink "${PREFIX}/current")"
  basename "${target}"
}

parse_args() {
  COMMAND="${1:-}"
  [[ -n "${COMMAND}" ]] || { usage; exit 1; }
  shift
  BUILD_DIR=""
  RELEASE_ID=""
  ROLLBACK_TO=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --prefix|--service|--cell-host|--cell-port|--expected-release|--output|--build-dir|--release-id|--rollback-to)
        [[ $# -ge 2 ]] || die "$1 needs a value"
        case "$1" in
          --prefix) PREFIX="$2";;
          --service) SERVICE="$2";;
          --cell-host) CELL_HOST="$2";;
          --cell-port) CELL_PORT="$2";;
          --expected-release) EXPECTED_RELEASE="$2";;
          --output) OUTPUT="$2";;
          --build-dir) BUILD_DIR="$2";;
          --release-id) RELEASE_ID="$2";;
          --rollback-to) ROLLBACK_TO="$2";;
        esac
        shift 2;;
      --restart) RESTART=1; shift;;
      -h|--help) usage; exit 0;;
      *) die "unknown argument: $1";;
    esac
  done
  is_abs "${PREFIX}" || die "prefix must be absolute"
}

print_systemd_status() {
  local unit="$1"
  if ! unit_exists; then
    echo "unit=${unit} service_active=UNAVAILABLE process_alive=UNAVAILABLE systemd=UNAVAILABLE"
    return 0
  fi
  local active enabled pid
  active="$(systemctl is-active "${unit}" 2>/dev/null || true)"
  enabled="$(systemctl is-enabled "${unit}" 2>/dev/null || true)"
  pid="$(systemctl show -p MainPID --value "${unit}" 2>/dev/null || true)"
  [[ -n "${pid}" ]] || pid=0
  local alive=false
  if [[ "${pid}" =~ ^[0-9]+$ && "${pid}" -gt 0 && -d "/proc/${pid}" ]]; then alive=true; fi
  echo "unit=${unit} service_active=${active:-UNAVAILABLE} enabled=${enabled:-UNAVAILABLE} main_pid=${pid} process_alive=${alive}"
}

cmd_status() {
  echo "prefix=${PREFIX} current=$(current_release_id)"
  local manifest="${PREFIX}/current/MANIFEST.txt"
  echo "release_id=$(manifest_value release_id "${manifest}")"
  echo "git_commit=$(manifest_value git_commit "${manifest}")"
  echo "git_dirty=$(manifest_value git_dirty "${manifest}")"
  print_systemd_status "rcrd.service"
  print_systemd_status "rcr-cell-app.service"
  print_systemd_status "rcr-modbus-rtu-agent.service"
}

cmd_observe() {
  [[ -x "${OBSERVE}" ]] || die "observe consumer missing: ${OBSERVE}"
  local args=(--prefix "${PREFIX}" --service "${SERVICE}"
    --cell-host "${CELL_HOST}" --cell-port "${CELL_PORT}")
  [[ -n "${EXPECTED_RELEASE}" ]] && args+=(--expected-release "${EXPECTED_RELEASE}")
  exec python3 "${OBSERVE}" "${args[@]}"
}

cmd_healthcheck() {
  local manifest="${PREFIX}/current/MANIFEST.txt"
  local current="$(current_release_id)"
  local active="UNAVAILABLE" pid=0 alive=UNAVAILABLE
  if unit_exists; then
    active="$(systemctl is-active "${SERVICE}" 2>/dev/null || true)"
    pid="$(systemctl show -p MainPID --value "${SERVICE}" 2>/dev/null || echo 0)"
    alive=false
    if [[ "${pid}" =~ ^[0-9]+$ && "${pid}" -gt 0 && -d "/proc/${pid}" ]]; then alive=true; fi
  fi
  local version_match=UNKNOWN
  if [[ -n "${EXPECTED_RELEASE}" ]]; then
    [[ "${current}" == "${EXPECTED_RELEASE}" ]] && version_match=true || version_match=false
  elif [[ -n "${current}" && -f "${manifest}" ]]; then
    [[ "$(manifest_value release_id "${manifest}")" == "${current}" ]] && version_match=true || version_match=false
  fi
  echo "process_alive=${alive}"
  echo "service_active=${active:-UNAVAILABLE}"
  echo "version_match=${version_match}"

  local runtime_reachable=UNKNOWN runtime_state=UNAVAILABLE device_health=UNAVAILABLE cell_io_health=UNAVAILABLE
  local probe_output=""
  if [[ "${SERVICE}" == "rcr-cell-app.service" && -x "${PROBE}" ]]; then
    if probe_output="$(python3 "${PROBE}" "${CELL_HOST}" "${CELL_PORT}" 2>/dev/null)"; then
      while IFS='=' read -r key value; do
        case "${key}" in
          runtime_reachable) runtime_reachable="${value}";;
          runtime_state) runtime_state="${value}";;
          device_health) device_health="${value}";;
          cell_io_health) cell_io_health="${value}";;
        esac
      done <<<"${probe_output}"
    else
      runtime_reachable=false
      runtime_state=UNAVAILABLE
      device_health=UNAVAILABLE
      cell_io_health=UNAVAILABLE
    fi
  fi
  echo "runtime_reachable=${runtime_reachable}"
  echo "runtime_state=${runtime_state}"
  echo "device_health=${device_health}"
  echo "cell_io_health=${cell_io_health}"
  local overall=DEGRADED
  if [[ "${alive}" == true && "${active}" == active && "${version_match}" == true &&
        "${runtime_reachable}" == true && "${device_health}" == HEALTHY &&
        "${cell_io_health}" == HEALTHY ]]; then
    overall=HEALTHY
  fi
  echo "overall=${overall}"
  [[ "${overall}" == HEALTHY ]]
}

try_collect() {
  local name="$1"; shift
  local out="${OUTPUT}/${name}"
  if "$@" >"${out}" 2>&1; then
    echo "collected=${name}" >>"${OUTPUT}/collector.log"
  else
    echo "error=${name}" >>"${OUTPUT}/collector.log"
    echo "command_failed=${name}" >>"${OUTPUT}/errors.txt"
  fi
}

cmd_collect_logs() {
  [[ -n "${OUTPUT}" ]] || die "collect-logs requires --output DIR"
  [[ ! -e "${OUTPUT}" ]] || die "refuse overwrite existing bundle: ${OUTPUT}"
  mkdir -p "${OUTPUT}"
  : >"${OUTPUT}/collector.log"
  : >"${OUTPUT}/errors.txt"
  {
    echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "monotonic_ns=$(awk '{print $1}' /proc/uptime 2>/dev/null || echo unavailable)"
    echo "hostname=$(hostname)"
    echo "kernel=$(uname -srvm)"
    echo "machine=$(uname -m)"
    echo "prefix=${PREFIX}"
    echo "current=$(current_release_id)"
  } >"${OUTPUT}/anchor.txt"
  if [[ -f "${PREFIX}/current/MANIFEST.txt" ]]; then
    cp -p "${PREFIX}/current/MANIFEST.txt" "${OUTPUT}/MANIFEST.txt"
  else
    echo "missing=MANIFEST.txt" >>"${OUTPUT}/errors.txt"
  fi

  # 配置摘要只记录本次所观察的 unit、release 和 systemd 入口；不导出环境变量，避免把
  # 将来可能出现的凭据混进 incident bundle。
  {
    echo "service=${SERVICE}"
    echo "prefix=${PREFIX}"
    echo "release_id=$(manifest_value release_id "${PREFIX}/current/MANIFEST.txt")"
    systemctl show -p FragmentPath -p ExecStart "${SERVICE}"
  } >"${OUTPUT}/configuration_summary.txt" 2>&1 || {
    echo "error=configuration_summary" >>"${OUTPUT}/collector.log"
    echo "command_failed=configuration_summary" >>"${OUTPUT}/errors.txt"
  }
  try_collect systemd_${SERVICE//[^A-Za-z0-9_.-]/_}.show systemctl show "${SERVICE}"
  try_collect systemd_${SERVICE//[^A-Za-z0-9_.-]/_}.status systemctl --no-pager --full status "${SERVICE}"
  try_collect journal_${SERVICE//[^A-Za-z0-9_.-]/_}.log journalctl --no-pager -n 200 -u "${SERVICE}"

  # systemd 是 PID 的 authority；只有能确认精确 PID 才读取其 /proc。权限或进程退出时
  # bundle 保持 partial，不能把 host 的任意进程误写成 Runtime snapshot。
  local service_pid=""
  if unit_exists; then
    service_pid="$(systemctl show -p MainPID --value "${SERVICE}" 2>/dev/null || true)"
  fi
  if [[ "${service_pid}" =~ ^[1-9][0-9]*$ && -d "/proc/${service_pid}" ]]; then
    echo "main_pid=${service_pid}" >"${OUTPUT}/process_target.txt"
    try_collect process_status cat "/proc/${service_pid}/status"
    try_collect process_threads ps -T -p "${service_pid}" -o pid,tid,stat,comm
    try_collect process_fds ls -l "/proc/${service_pid}/fd"
    try_collect process_scheduler chrt -p "${service_pid}"
  else
    printf 'availability=UNAVAILABLE reason=service_main_pid_unavailable value=%s\n' \
      "${service_pid:-none}" >"${OUTPUT}/process_target.txt"
    echo "error=process_snapshot" >>"${OUTPUT}/collector.log"
    echo "source_unavailable=process_snapshot" >>"${OUTPUT}/errors.txt"
  fi
  if [[ -d "/proc" ]]; then
    try_collect proc_meminfo cat /proc/meminfo
    try_collect proc_cpuinfo head -200 /proc/cpuinfo
  fi
  try_collect can_interfaces ip -details link show type can
  if [[ "${SERVICE}" == "rcr-cell-app.service" ]]; then
    try_collect cel1_status python3 "${PROBE}" "${CELL_HOST}" "${CELL_PORT}"
  fi

  # RuntimeDaemon 的 final summary 已在 bounded journal 中。若服务尚未产生该行，保留
  # UNKNOWN/partial，而不是从 process_alive 推断一个最终 Runtime 状态。
  local journal_file="${OUTPUT}/journal_${SERVICE//[^A-Za-z0-9_.-]/_}.log"
  if [[ -f "${journal_file}" ]] && grep -F 'final summary ' "${journal_file}" | tail -n 1 \
      >"${OUTPUT}/latest_final_summary.txt"; then
    if [[ ! -s "${OUTPUT}/latest_final_summary.txt" ]]; then
      printf 'availability=UNAVAILABLE reason=final_summary_not_found\n' \
        >"${OUTPUT}/latest_final_summary.txt"
      echo "source_unavailable=latest_final_summary" >>"${OUTPUT}/errors.txt"
    fi
  else
    printf 'availability=UNAVAILABLE reason=journal_unavailable\n' \
      >"${OUTPUT}/latest_final_summary.txt"
    echo "source_unavailable=latest_final_summary" >>"${OUTPUT}/errors.txt"
  fi
  local complete=true
  [[ ! -s "${OUTPUT}/errors.txt" ]] || complete=false
  echo "complete=${complete}" >"${OUTPUT}/BUNDLE_STATUS"
  # SHA256SUMS 不能递归包含自身；BUNDLE_STATUS 已写入，因此其完整性仍在清单内。
  find "${OUTPUT}" -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z |
    xargs -0 sha256sum >"${OUTPUT}/SHA256SUMS"
  echo "bundle=${OUTPUT} complete=${complete}"
}

restart_exact() {
  [[ "${RESTART}" -eq 1 ]] || return 0
  unit_exists || die "--restart requested but systemctl is unavailable"
  systemctl try-restart "${SERVICE}"
}

cmd_deploy_or_upgrade() {
  [[ -x "${INSTALLER}" ]] || die "installer missing: ${INSTALLER}"
  [[ -n "${BUILD_DIR}" ]] || die "${COMMAND} requires --build-dir"
  local previous="$(current_release_id)"
  local args=(--apply --activate --prefix "${PREFIX}" --build-dir "${BUILD_DIR}")
  [[ -n "${RELEASE_ID}" ]] && args+=(--release-id "${RELEASE_ID}")
  "${INSTALLER}" "${args[@]}"
  restart_exact
  if [[ "${COMMAND}" == upgrade ]]; then
    if cmd_healthcheck; then
      echo "upgrade_result=healthy"
    else
      echo "upgrade_result=healthcheck_failed" >&2
      if [[ -n "${ROLLBACK_TO}" ]]; then
        "${ROLLBACK}" --apply --prefix "${PREFIX}" "${ROLLBACK_TO}"
        restart_exact
        # 回滚切 symlink 不等于服务已恢复。临时收紧 expected release，再用同一只读
        # health contract 复核；失败必须向上返回，避免把 last-known-good 写成猜测。
        local saved_expected="${EXPECTED_RELEASE}"
        EXPECTED_RELEASE="${ROLLBACK_TO}"
        if ! cmd_healthcheck; then
          EXPECTED_RELEASE="${saved_expected}"
          echo "rollback_result=healthcheck_failed" >&2
          return 4
        fi
        EXPECTED_RELEASE="${saved_expected}"
        echo "rollback_result=healthy"
      elif [[ -n "${previous}" ]]; then
        echo "rollback_required=explicit target ${previous}" >&2
      fi
      return 3
    fi
  fi
}

cmd_rollback() {
  [[ -n "${ROLLBACK_TO}" ]] || die "rollback requires --rollback-to ID"
  "${ROLLBACK}" --apply --prefix "${PREFIX}" "${ROLLBACK_TO}"
  restart_exact
}

parse_args "$@"
case "${COMMAND}" in
  status) cmd_status;;
  observe) cmd_observe;;
  healthcheck) cmd_healthcheck;;
  collect-logs) cmd_collect_logs;;
  deploy|upgrade) cmd_deploy_or_upgrade;;
  rollback) cmd_rollback;;
  *) die "unknown command: ${COMMAND}";;
esac
