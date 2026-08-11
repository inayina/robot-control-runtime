#!/usr/bin/env python3
"""Restore stock boot.scr/cmd via serial shell + debugfs after failed dual install."""
from __future__ import annotations

import sys
import time

import serial

PORT = "/dev/ttyUSB0"
BAUD = 115200


def main() -> int:
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
    time.sleep(0.3)

    def rw(timeout: float = 2.0) -> bytes:
        t0 = time.time()
        buf = b""
        while time.time() - t0 < timeout:
            chunk = ser.read(4096)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                buf += chunk
            else:
                time.sleep(0.05)
        return buf

    def send(s: str, wait: float = 1.5) -> bytes:
        print(f"\n>>> {s[:120]!r}", flush=True)
        ser.write((s + "\r").encode())
        ser.flush()
        return rw(wait)

    for _ in range(5):
        ser.write(b"\r")
        time.sleep(0.15)
    out = rw(2.5)
    print("\n--- probe done ---", flush=True)

    # Auto-login may already be at shell; otherwise try common creds.
    if b"login:" in out.lower():
        send("orangepi", 1.5)
        send("orangepi", 2.5)

    out = send("id; whoami; mount | awk '$2==\"/\"{print}'; echo READY_MARK", 4)
    if b"READY_MARK" not in out:
        send("orangepi", 2)
        out = send("id; echo READY_MARK2", 3)

    # One-liner restore to avoid nested heredoc over serial.
    restore = (
        "python3 -c \""
        "import os,subprocess,tempfile;"
        "dev='/dev/mmcblk1p1';"
        "td=tempfile.mkdtemp();"
        "subprocess.check_call(['debugfs','-R',f'dump /boot/boot.scr.pre-dual.20260810T131320Z {td}/boot.scr',dev]);"
        "subprocess.check_call(['debugfs','-R',f'dump /boot/boot.cmd.pre-dual.20260810T131320Z {td}/boot.cmd',dev]);"
        "print('sizes',os.path.getsize(f'{td}/boot.scr'),os.path.getsize(f'{td}/boot.cmd'));"
        "open(f'{td}/cmds','w').write("
        "f'cd boot\\nrm boot.scr\\nrm boot.cmd\\nwrite {td}/boot.scr boot.scr\\n"
        "write {td}/boot.cmd boot.cmd\\nquit\\n'"
        ");"
        "r=subprocess.run(['debugfs','-w','-f',f'{td}/cmds',dev],capture_output=True,text=True);"
        "print(r.stdout);print(r.stderr);print('rc',r.returncode);"
        "subprocess.check_call(['sync']);"
        "subprocess.check_call(['debugfs','-R','stat /boot/boot.scr',dev]);"
        "print('RESTORED_OK')"
        "\""
    )
    send(restore, 12)
    send("dmesg | grep -iE 'ext4|remount' | tail -30; echo DONEFIX", 5)
    ser.close()
    print("\n=== end ===", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
