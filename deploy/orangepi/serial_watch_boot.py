#!/usr/bin/env python3
"""Capture Orange Pi serial until dual-boot flavor line + login (or panic)."""
from __future__ import annotations

import argparse
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=180.0)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    t0 = time.time()
    buf = b""
    print("=== serial watch start ===", flush=True)
    while time.time() - t0 < args.timeout:
        chunk = ser.read(4096)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            buf += chunk
            if b"Kernel panic" in buf:
                print("\n=== PANIC ===", flush=True)
                ser.close()
                return 2
            if b"Selected kernel_flavor=" in buf and b"login:" in buf:
                print("\n=== flavor+login seen ===", flush=True)
                ser.close()
                return 0
        else:
            time.sleep(0.02)
    ser.close()
    print("\n=== TIMEOUT ===", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
