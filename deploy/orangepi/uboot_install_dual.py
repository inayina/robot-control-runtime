#!/usr/bin/env python3
"""Install dual-boot.scr/env via U-Boot serial (no Linux sudo).

Expects USB-TTL on /dev/ttyUSB0 and dual-boot files already on the board under
/home/orangepi/dual-boot/. Leaves kernel_flavor=stock as default.
"""
from __future__ import annotations

import argparse
import sys
import time

import serial


def wait_for(ser: serial.Serial, needles, timeout: float = 60.0) -> tuple[bool, bytes]:
    if isinstance(needles, (str, bytes)):
        needles = [needles]
    needles_b = [n.encode() if isinstance(n, str) else n for n in needles]
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        chunk = ser.read(4096)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            buf += chunk
            for n in needles_b:
                if n in buf:
                    return True, buf
        else:
            time.sleep(0.02)
    return False, buf


def send(ser: serial.Serial, cmd: str, delay: float = 0.25) -> None:
    ser.write((cmd + "\r").encode())
    ser.flush()
    time.sleep(delay)


def at_prompt(ser: serial.Serial, timeout: float = 8.0) -> bool:
    send(ser, "")
    ok, _ = wait_for(ser, [b"=>"], timeout=timeout)
    return ok


def run_cmd(ser: serial.Serial, cmd: str, timeout: float = 45.0) -> bytes:
    print(f"\n>>> {cmd}", flush=True)
    send(ser, cmd, delay=0.15)
    ok, buf = wait_for(ser, [b"=>"], timeout=timeout)
    if not ok:
        raise RuntimeError(f"timeout after command: {cmd}")
    return buf


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument(
        "--flavor-env",
        default="orangepiEnv.stock",
        help="env file under /home/orangepi/dual-boot/ to install as orangepiEnv.txt",
    )
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(0.2)
    ser.reset_input_buffer()
    print("=== waiting for U-Boot autoboot prompt ===", flush=True)
    ok, _ = wait_for(
        ser,
        [b"Hit any key to stop autoboot", b"U-Boot 20", b"U-Boot SPL"],
        timeout=120,
    )
    if not ok:
        print("TIMEOUT waiting for U-Boot", flush=True)
        return 1

    # Stop autoboot.
    for _ in range(8):
        ser.write(b"\r")
        time.sleep(0.05)
    time.sleep(0.4)
    if not at_prompt(ser, timeout=6):
        for _ in range(20):
            ser.write(b"\r")
            time.sleep(0.08)
        if not at_prompt(ser, timeout=6):
            print("No U-Boot prompt", flush=True)
            return 2

    print("\n=== U-Boot prompt OK; probing boot device ===", flush=True)
    env_buf = run_cmd(ser, "printenv devtype devnum prefix bootcmd kernel_addr_r")

    # Prefer values already set by vendor boot flow; fall back to Orange Pi eMMC.
    # Most sunxi images use mmc 2:1 with prefix=/boot/.
    run_cmd(ser, "ext4ls mmc 2:1 /home/orangepi/dual-boot")
    run_cmd(ser, "ext4ls mmc 2:1 /boot")

    cmds = [
        # Backup current boot.scr if readable (best-effort).
        "load mmc 2:1 ${kernel_addr_r} /boot/boot.scr",
        "ext4write mmc 2:1 ${kernel_addr_r} /boot/boot.scr.pre-dual ${filesize}",
        "load mmc 2:1 ${kernel_addr_r} /boot/boot.cmd",
        "ext4write mmc 2:1 ${kernel_addr_r} /boot/boot.cmd.pre-dual ${filesize}",
        # Install dual boot artifacts.
        "load mmc 2:1 ${kernel_addr_r} /home/orangepi/dual-boot/boot-sun60iw2-dual.scr",
        "ext4write mmc 2:1 ${kernel_addr_r} /boot/boot.scr ${filesize}",
        "load mmc 2:1 ${kernel_addr_r} /home/orangepi/dual-boot/boot-sun60iw2-dual.cmd",
        "ext4write mmc 2:1 ${kernel_addr_r} /boot/boot.cmd ${filesize}",
        f"load mmc 2:1 ${{kernel_addr_r}} /home/orangepi/dual-boot/{args.flavor_env}",
        "ext4write mmc 2:1 ${kernel_addr_r} /boot/orangepiEnv.txt ${filesize}",
        "ext4ls mmc 2:1 /boot",
    ]
    for c in cmds:
        try:
            run_cmd(ser, c, timeout=60)
        except RuntimeError as exc:
            print(f"WARN: {exc}", flush=True)

    print("\n=== booting (expect Selected kernel_flavor=stock) ===", flush=True)
    send(ser, "boot", delay=0.3)
    wait_for(
        ser,
        [b"Selected kernel_flavor=", b"login:", b"Debian GNU", b"Starting kernel"],
        timeout=180,
    )
    # Keep capturing a bit more for diagnosis.
    wait_for(ser, [b"login:"], timeout=90)
    ser.close()
    print("\n=== serial helper done ===", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
