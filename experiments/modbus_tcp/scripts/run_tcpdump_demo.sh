#!/usr/bin/env bash
# Modbus TCP localhost 抓包演示说明 / 可选 tcpdump。
# 用法：
#   ./experiments/modbus_tcp/scripts/run_tcpdump_demo.sh --dry-run
#   sudo ./experiments/modbus_tcp/scripts/run_tcpdump_demo.sh
#   sudo ./experiments/modbus_tcp/scripts/run_tcpdump_demo.sh --pcap evidence/modbus_tcp/capture_demo.pcap
#
# 本脚本不启动 server/client（避免抢端口/混进自动化）。先另开：
#   ./build/modbus_tcp/mbus_ref_server --port 1502
#   ./build/modbus_tcp/mbus_demo_client --host 127.0.0.1 --port 1502
# 无 CAP_NET_RAW/root 时用 --dry-run；不要把本机 hex 当成现场证据。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PORT="${RCR_MBUS_PORT:-1502}"
IFACE="${RCR_MBUS_TCPDUMP_IFACE:-lo}"
DRY_RUN=0
PCAP=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    --pcap) PCAP="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --iface) IFACE="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,14p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

print_expected() {
  cat <<EOF
=== Modbus TCP capture-vs-bytes (operator checklist) ===
iface=${IFACE}  filter: tcp port ${PORT}
repo=${ROOT}

1) Terminal A:  ${ROOT}/build/modbus_tcp/mbus_ref_server --port ${PORT}
   expect log: holding[0]=0x1234 holding[1]=0xABCD

2) Start capture (this script with sudo, or):
   sudo tcpdump -i ${IFACE} -nn -X -s0 'tcp port ${PORT}'

3) Terminal B:  ${ROOT}/build/modbus_tcp/mbus_demo_client --host 127.0.0.1 --port ${PORT}
   expect log:
     read holding[0..3]: 0x1234 0xabcd 0x0 0x0
     write_multiple ok addr=20 qty=3

MBAP (7B): TransID | ProtoID=0000 | Length | UnitID
Match request/response TransID; registers are big-endian uint16.

Demo ADU payloads (UnitID=00; TransID starts at 0001 and increments):

  [1] Write Single 0x06 addr=10 value=0xBEEF
      MBAP Length=0006  PDU: 06 00 0A BE EF
      response PDU echoes the same 5 bytes

  [2] Read Holding 0x03 addr=0 qty=4
      MBAP Length=0006  PDU: 03 00 00 00 04
      response PDU: 03 08 12 34 AB CD 00 00 00 00   (if seed unchanged)

  [3] Write Multiple 0x10 addr=20 qty=3 values=1,2,3
      MBAP Length=000D  PDU: 10 00 14 00 03 06 00 01 00 02 00 03
      response PDU: 10 00 14 00 03  Length=0006

Wireshark: open pcap, filter tcp.port == ${PORT}; decode MBAP by hand if no Modbus dissector.
pcap under evidence/modbus_tcp/ is gitignored; not a formal Gate.
EOF
}

print_expected

if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo
  echo "dry-run: not invoking tcpdump (no sudo / CAP_NET_RAW required)."
  exit 0
fi

if [[ "$(id -u)" -ne 0 ]]; then
  echo >&2
  echo "error: live capture needs root (or CAP_NET_RAW). Re-run with sudo, or --dry-run." >&2
  exit 1
fi

if ! command -v tcpdump >/dev/null 2>&1; then
  echo "error: tcpdump not found in PATH" >&2
  exit 1
fi

echo
if [[ -n "${PCAP}" ]]; then
  mkdir -p "$(dirname "${PCAP}")"
  echo "capturing to ${PCAP} (Ctrl-C to stop)..."
  exec tcpdump -i "${IFACE}" -nn -s0 -w "${PCAP}" "tcp port ${PORT}"
else
  echo "live hex dump (Ctrl-C to stop)..."
  exec tcpdump -i "${IFACE}" -nn -X -s0 "tcp port ${PORT}"
fi
