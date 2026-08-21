#!/usr/bin/env python3
"""CEL1 status reader and explicit no-write Cell I/O recovery probe.

By default this sends frozen GetStatus only.  --probe-cell-io asks rcr_cell_app
to issue one localhost-agent FC02/FC01 check and synchronize its I/O snapshot.
Neither mode activates the Runtime, submits output, or opens SocketCAN/serial
from this operations client; the explicit probe never sends FC05.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys


MAGIC = 0x314C4543
VERSION = 1
GET_STATUS = 1
GET_STATUS_ACK = 2
PROBE_CELL_IO = 8
PROBE_CELL_IO_ACK = 9
HEADER_SIZE = 12
STATUS_SIZE = 80


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def frame(message_type: int, sequence: int = 1, payload: bytes = b"") -> bytes:
    header = struct.pack(
        "<IBBBBHH", MAGIC, VERSION, message_type, 0, 0, sequence, len(payload)
    )
    body = header + payload
    return body + struct.pack("<H", crc16(body))


def read_frame(sock: socket.socket) -> tuple[int, bytes]:
    header = b""
    while len(header) < HEADER_SIZE:
        chunk = sock.recv(HEADER_SIZE - len(header))
        if not chunk:
            raise RuntimeError("short CEL1 header")
        header += chunk
    magic, version, message_type, _reserved0, _reserved1, _sequence, size = struct.unpack(
        "<IBBBBHH", header
    )
    if magic != MAGIC or version != VERSION:
        raise RuntimeError("invalid CEL1 header")
    if size > 256:
        raise RuntimeError("CEL1 payload too large")
    tail = b""
    while len(tail) < size + 2:
        chunk = sock.recv(size + 2 - len(tail))
        if not chunk:
            raise RuntimeError("CEL1 peer closed")
        tail += chunk
    payload = tail[:size]
    received_crc = struct.unpack("<H", tail[size:])[0]
    if crc16(header + payload) != received_crc:
        raise RuntimeError("CEL1 CRC mismatch")
    return message_type, payload


def mode_name(value: int) -> str:
    return ("DISABLED", "IDLE", "ACTIVE", "HOLD", "FAULT", "ESTOP", "UNKNOWN")[
        value if value < 7 else 6
    ]


def fault_name(value: int) -> str:
    return (
        "NONE",
        "WATCHDOG",
        "INPUT_FAULT",
        "COMM_LOSS",
        "NODE_FAULT",
        "PROTOCOL_REJECT",
        "INTERLOCK_LOST",
        "INTERNAL",
        "ACK_TIMEOUT",
        "UNKNOWN",
    )[value if value < 10 else 9]


def evidence_name(value: int) -> str:
    return ("UNSPECIFIED", "MOCK", "VCAN", "PHYSICAL", "LOOPBACK")[
        value if value < 5 else 0
    ]


def ack_name(value: int) -> str:
    return (
        "APPLIED",
        "STALE",
        "SESSION_MISMATCH",
        "EXPIRED",
        "INVALID_MASK",
        "NOT_READY",
        "UNKNOWN",
    )[value if value < 7 else 6]


def probe(host: str, port: int, timeout: float, probe_cell_io: bool = False) -> dict[str, object]:
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(frame(PROBE_CELL_IO if probe_cell_io else GET_STATUS))
        message_type, payload = read_frame(sock)
    expected = PROBE_CELL_IO_ACK if probe_cell_io else GET_STATUS_ACK
    if message_type != expected or len(payload) != STATUS_SIZE:
        raise RuntimeError("unexpected CEL1 reply")

    # Layout is frozen by cell_app_protocol.hpp.  The probe exposes only the
    # read-only projection; it does not reinterpret the Runtime authority.
    observed_ns = struct.unpack_from("<q", payload, 0)[0]
    mode = payload[8]
    fault = payload[9]
    started = payload[10] != 0
    interlock = payload[11] != 0
    online = payload[12] != 0
    position_reached = payload[13] != 0
    cell_ready = payload[14] != 0
    node_id = payload[15]
    boot_id = struct.unpack_from("<H", payload, 16)[0]
    session_id = struct.unpack_from("<H", payload, 18)[0]
    last_heartbeat_sequence = struct.unpack_from("<H", payload, 20)[0]
    heartbeat_age_ns = struct.unpack_from("<q", payload, 22)[0]
    input_bits = struct.unpack_from("<H", payload, 30)[0]
    device_fault_code = struct.unpack_from("<H", payload, 32)[0]
    frames_received = struct.unpack_from("<Q", payload, 34)[0]
    frames_sent = struct.unpack_from("<Q", payload, 42)[0]
    decode_rejects = struct.unpack_from("<Q", payload, 50)[0]
    input_queue_drop_count = struct.unpack_from("<Q", payload, 58)[0]
    last_ack_session = struct.unpack_from("<H", payload, 66)[0]
    last_ack_sequence = struct.unpack_from("<H", payload, 68)[0]
    last_ack_result = payload[70]
    ack_pending = payload[71] != 0
    evidence = payload[72]
    modbus_online = payload[73] != 0
    do0_requested = payload[74] != 0
    do0_confirmed = payload[75] != 0
    return {
        "source_owner": "rcr_cell_app/CEL1",
        "cell_io_probe_requested": probe_cell_io,
        "runtime_reachable": True,
        "runtime_state": mode_name(mode),
        "runtime_fault": fault_name(fault),
        "runtime_fault_code": fault,
        "runtime_started": started,
        "interlock_ready": interlock,
        "device_online": online,
        "device_health": "HEALTHY" if online else "DEGRADED",
        "position_reached": position_reached,
        "cell_ready": cell_ready,
        "node_id": node_id,
        "boot_id": boot_id,
        "session_id": session_id,
        "last_heartbeat_sequence": last_heartbeat_sequence,
        "heartbeat_age_ns": heartbeat_age_ns,
        "input_bits": input_bits,
        "device_fault_code": device_fault_code,
        "frames_received": frames_received,
        "frames_sent": frames_sent,
        "decode_rejects": decode_rejects,
        "input_queue_drop_count": input_queue_drop_count,
        "last_ack_session": last_ack_session,
        "last_ack_sequence": last_ack_sequence,
        "last_ack_result": ack_name(last_ack_result),
        "last_ack_result_code": last_ack_result,
        "ack_pending": ack_pending,
        "evidence": evidence_name(evidence),
        "modbus_online": modbus_online,
        "cell_ready_do0_requested": do0_requested,
        "cell_ready_do0_confirmed": do0_confirmed,
        "cell_ready_do0_status": payload[76],
        "cell_io_health": "HEALTHY" if modbus_online and do0_confirmed == cell_ready else "DEGRADED",
        "observed_monotonic_ns": observed_ns,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--probe-cell-io", action="store_true",
                        help="explicit FC02/FC01 recovery probe; never writes DO0")
    args = parser.parse_args()
    try:
        result = probe(args.host, args.port, args.timeout, args.probe_cell_io)
    except (OSError, RuntimeError, struct.error) as exc:
        print(f"error={exc}")
        return 2
    for key, value in result.items():
        if isinstance(value, bool):
            value = str(value).lower()
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
