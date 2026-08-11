#!/usr/bin/env python3
"""Wait for Orange Pi power-cycle, then restore stock boot and boot Linux.

Run this BEFORE power-cycling (or right as you power on). It catches BOOT0/U-Boot,
writes stock boot.scr to filesystem root via ext4write, restores /boot via a second
pass after Linux is up is done separately.
"""
from __future__ import annotations

import sys
import time

import serial


def main() -> int:
    print("=== waiting for power-cycle / BOOT0 (plug power now) ===", flush=True)
    ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2)
    # Do not SysRq; wait for fresh power-on.
    t_deadline = time.time() + 300

    def read_until(needles, timeout: float) -> bytes:
        if isinstance(needles, (str, bytes)):
            needles = [needles]
        needles_b = [n.encode() if isinstance(n, str) else n for n in needles]
        buf = b""
        t0 = time.time()
        while time.time() - t0 < timeout and time.time() < t_deadline:
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
        ser.reset_input_buffer()
        print(f"\n>>> {c}", flush=True)
        ser.write((c + "\r").encode())
        ser.flush()
        return read_until(b"=>", timeout=timeout)

    buf = read_until([b"BOOT0 is starting", b"Hit any key to stop autoboot", b"U-Boot 2018"], timeout=300)
    if not buf:
        print("TIMEOUT waiting for power-on", flush=True)
        ser.close()
        return 2

    # Interrupt autoboot
    for _ in range(20):
        ser.write(b"\r")
        time.sleep(0.05)
    if b"=>" not in read_until(b"=>", timeout=8):
        print("NO_PROMPT", flush=True)
        ser.close()
        return 3

    print("=== U-Boot prompt OK ===", flush=True)

    # Put known-good stock script at FS root so distro boot can find /boot.scr
    # even if /boot/boot.scr is the broken dual image.
    out = cmd("load mmc 0:1 ${kernel_addr_r} /boot/stock-1.0.8/boot.scr")
    if b"bytes read" not in out:
        print("FAIL load stock boot.scr", flush=True)
        ser.close()
        return 4
    cmd("ext4write mmc 0:1 ${kernel_addr_r} /boot.scr ${filesize}")

    # Manual boot with forced fsck (most reliable).
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
    for c in [
        "load mmc 0:1 ${ramdisk_addr_r} /boot/uInitrd-6.6.98-sun60iw2",
        "load mmc 0:1 ${kernel_addr_r} /boot/uImage",
        "load mmc 0:1 ${fdt_addr_r} /boot/dtb/allwinner/sun60i-a733-orangepi-4-pro.dtb",
        "fdt addr ${fdt_addr_r}",
        "fdt resize 65536",
    ]:
        out = cmd(c, timeout=90)
        if c.startswith("load") and b"bytes read" not in out:
            print(f"FAIL {c}", flush=True)
            ser.close()
            return 5

    print("\n>>> bootm", flush=True)
    ser.reset_input_buffer()
    ser.write(b"bootm ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}\r")
    ser.flush()
    out = read_until([b"login:", b"Kernel panic", b"Give root password"], timeout=360)
    ser.close()
    if b"login:" in out:
        print("\n=== BOOT_OK ===", flush=True)
        return 0
    print("\n=== BOOT_UNCERTAIN ===", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
