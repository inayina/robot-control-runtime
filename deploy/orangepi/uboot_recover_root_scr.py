#!/usr/bin/env python3
"""Recover Orange Pi: write stock boot.scr to FS root, then manual stock boot.

Fixes a prior helper bug where wait_for matched stale '=>' in the buffer.
"""
from __future__ import annotations

import sys
import time

import serial


def main() -> int:
    ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2)
    time.sleep(0.2)
    ser.reset_input_buffer()

    def read_until(needles, timeout: float) -> bytes:
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
                        return buf
            else:
                time.sleep(0.02)
        return buf

    def cmd(c: str, timeout: float = 60.0) -> bytes:
        # Drain any pending output before sending.
        ser.reset_input_buffer()
        print(f"\n>>> {c}", flush=True)
        ser.write((c + "\r").encode())
        ser.flush()
        return read_until(b"=>", timeout=timeout)

    def at_prompt(timeout: float = 5.0) -> bool:
        ser.reset_input_buffer()
        ser.write(b"\r")
        ser.flush()
        return b"=>" in read_until(b"=>", timeout=timeout)

    print("=== try SysRq reboot ===", flush=True)
    ser.send_break(duration=0.3)
    time.sleep(0.15)
    ser.write(b"b")
    ser.flush()

    buf = read_until(
        [b"Hit any key to stop autoboot", b"U-Boot 2018", b"BOOT0 is starting", b"=>"],
        timeout=90,
    )
    if not buf:
        print("NO_BOOT_SIGNAL: power-cycle the board, then re-run this script", flush=True)
        ser.close()
        return 2

    # Stop autoboot
    for _ in range(15):
        ser.write(b"\r")
        time.sleep(0.05)
    if not at_prompt(6):
        print("NO_PROMPT", flush=True)
        ser.close()
        return 3
    print("=== at U-Boot prompt ===", flush=True)

    # Write stock artifacts to partition root (ext4write limitation).
    out = cmd("load mmc 0:1 ${kernel_addr_r} /boot/stock-1.0.8/boot.scr")
    if b"bytes read" not in out:
        print("LOAD_STOCK_SCR_FAILED", flush=True)
        ser.close()
        return 4
    out = cmd("ext4write mmc 0:1 ${kernel_addr_r} /boot.scr ${filesize}")
    print("ext4write boot.scr ->", "ok" if b"=>" in out else out[-200:], flush=True)
    cmd("load mmc 0:1 ${kernel_addr_r} /boot/stock-1.0.8/boot.cmd")
    cmd("ext4write mmc 0:1 ${kernel_addr_r} /boot.cmd ${filesize}")
    cmd("load mmc 0:1 ${kernel_addr_r} /boot/stock-1.0.8/orangepiEnv.txt")
    cmd("ext4write mmc 0:1 ${kernel_addr_r} /orangepiEnv.txt ${filesize}")
    cmd("ext4ls mmc 0:1 /")

    # Manual stock boot with forced fsck.
    cmd(
        "setenv consoleargs 'console=ttyS0,115200 console=tty1 "
        "earlyprintk=sunxi-uart,0x02500000 initcall_debug=0 splash=verbose'"
    )
    cmd(
        "setenv bootargs 'root=UUID=81941f83-3abf-46ef-81f7-cfa8c687d3da "
        "rootwait rootfstype=ext4 ${consoleargs} consoleblank=0 loglevel=7 "
        "clk_ignore_unused swiotlb=65536 fsck.mode=force fsck.repair=yes "
        "cgroup_enable=cpuset cgroup_memory=1 cgroup_enable=memory swapaccount=1'"
    )
    if b"bytes read" not in cmd(
        "load mmc 0:1 ${ramdisk_addr_r} /boot/uInitrd-6.6.98-sun60iw2", timeout=90
    ):
        print("LOAD_INITRD_FAILED", flush=True)
        ser.close()
        return 5
    if b"bytes read" not in cmd("load mmc 0:1 ${kernel_addr_r} /boot/uImage", timeout=90):
        print("LOAD_UIMAGE_FAILED", flush=True)
        ser.close()
        return 6
    cmd(
        "load mmc 0:1 ${fdt_addr_r} /boot/dtb/allwinner/sun60i-a733-orangepi-4-pro.dtb",
        timeout=30,
    )
    cmd("fdt addr ${fdt_addr_r}")
    cmd("fdt resize 65536")

    print("\n>>> bootm", flush=True)
    ser.reset_input_buffer()
    ser.write(b"bootm ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}\r")
    ser.flush()
    out = read_until(
        [b"login:", b"Kernel panic", b"Give root password", b"Cannot open root"],
        timeout=300,
    )
    ser.close()
    if b"login:" in out:
        print("\n=== BOOT_OK login seen ===", flush=True)
        return 0
    print("\n=== BOOT_UNCERTAIN ===", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
