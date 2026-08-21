#!/usr/bin/env bash
# LD2 本机合同演练：使用临时 prefix、fake systemd 和 CEL1 GetStatus。
# 不安装 unit、不修改 host systemd、不打开 CAN/串口；返回 77 表示环境不支持 localhost。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build/ld2-qt-off}"
PREFIX="$(mktemp -d /tmp/rcr-operations-test.XXXXXX)"
TEST_PID=""

cleanup() {
  [[ -z "${TEST_PID}" ]] || kill "${TEST_PID}" 2>/dev/null || true
  rm -rf "${PREFIX}"
}
trap cleanup EXIT

for name in rcrd rcr_node_sim rcr_benchmark rcr_modbus_rtu_agent rcr_cell_app; do
  [[ -x "${BUILD_DIR}/${name}" ]] || {
    echo "skip: missing ${BUILD_DIR}/${name}" >&2
    exit 77
  }
done

TEST_PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()' 2>/dev/null || true)"
if [[ -z "${TEST_PORT}" ]]; then
  echo "skip: localhost bind unavailable" >&2
  exit 77
fi
export TEST_PORT

sleep 30 & TEST_PID=$!
export TEST_PID
systemctl() {
  case "$1" in
    is-active) echo active;;
    is-enabled) echo enabled;;
    show)
      if [[ "${4:-}" == "--value" || "${3:-}" == "--value" ]]; then
        echo "${TEST_PID}"
      else
        echo "MainPID=${TEST_PID}"
      fi
      ;;
    try-restart) :;;
    status) echo "fake status $2";;
    *) return 1;;
  esac
}
export -f systemctl

python3 - <<'PY' &
import socket
import struct

def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xa001) if crc & 1 else crc >> 1
    return crc & 0xffff

payload = bytearray(80)
struct.pack_into('<q', payload, 0, 123)
payload[8] = 1       # IDLE
payload[10] = 1      # started
payload[11] = 1      # interlock_ready
payload[12] = 1      # device online
payload[14] = 1      # cell_ready
payload[73] = 1      # Modbus online
payload[75] = 1      # DO0 confirmed
header = struct.pack('<IBBBBHH', 0x314c4543, 1, 2, 0, 0, 1, 80)
wire = header + payload
wire += struct.pack('<H', crc16(wire))
server = socket.socket()
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('127.0.0.1', int(__import__('os').environ['TEST_PORT'])))
server.listen(8)
for _ in range(8):
    conn, _ = server.accept()
    conn.recv(512)
    conn.sendall(wire)
    conn.close()
server.close()
PY
sleep 0.1

"${ROOT}/deploy/orangepi/install_release.sh" --apply --activate \
  --prefix "${PREFIX}" --build-dir "${BUILD_DIR}" --release-id 1111111 >/dev/null
"${ROOT}/deploy/orangepi/rcr_operations.sh" healthcheck \
  --prefix "${PREFIX}" --service rcr-cell-app.service --cell-port "${TEST_PORT}" \
  --expected-release 1111111 >/dev/null
OBSERVE_JSON="$("${PREFIX}/current/bin/rcr_operations.sh" observe \
  --prefix "${PREFIX}" --service rcr-cell-app.service --cell-port "${TEST_PORT}" \
  --expected-release 1111111)"
python3 -c 'import json, sys; value=json.load(sys.stdin); assert value["schema"] == "rcr.local_observability.v1"; assert value["runtime"]["availability"] == "AVAILABLE"; assert value["release"]["version_match"] is True' <<<"${OBSERVE_JSON}"

"${ROOT}/deploy/orangepi/rcr_operations.sh" collect-logs \
  --prefix "${PREFIX}" --service rcrd.service --output "${PREFIX}/bundle" >/dev/null
[[ -f "${PREFIX}/bundle/BUNDLE_STATUS" ]]
[[ -f "${PREFIX}/bundle/configuration_summary.txt" ]]
[[ -f "${PREFIX}/bundle/process_target.txt" ]]
[[ -f "${PREFIX}/bundle/process_threads" ]]
[[ -f "${PREFIX}/bundle/process_fds" ]]
[[ -f "${PREFIX}/bundle/process_scheduler" ]]
[[ -f "${PREFIX}/bundle/latest_final_summary.txt" ]]
! grep -q 'SHA256SUMS$' "${PREFIX}/bundle/SHA256SUMS"
sha256sum -c "${PREFIX}/bundle/SHA256SUMS" >/dev/null

set +e
"${ROOT}/deploy/orangepi/rcr_operations.sh" upgrade \
  --prefix "${PREFIX}" --service rcr-cell-app.service --cell-port "${TEST_PORT}" \
  --build-dir "${BUILD_DIR}" --release-id 2222222 --expected-release 1111111 \
  --rollback-to 1111111 >/dev/null
UPGRADE_STATUS=$?
set -e
[[ "${UPGRADE_STATUS}" -eq 3 ]]
[[ "$(readlink "${PREFIX}/current")" == releases/1111111 ]]
"${ROOT}/deploy/orangepi/rcr_operations.sh" healthcheck \
  --prefix "${PREFIX}" --service rcr-cell-app.service --cell-port "${TEST_PORT}" \
  --expected-release 1111111 >/dev/null
echo "operations_contract=pass evidence=LOCAL_LOOPBACK_TEMP_PREFIX"
