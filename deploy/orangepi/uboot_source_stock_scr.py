#!/usr/bin/env python3
"""Stop U-Boot autoboot and source stock boot.scr backup."""
from __future__ import annotations

import sys
import time

import serial


def main() -> int:
    ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2)
    time.sleep(0.1)

    def wait_for(needles, timeout: float = 90.0) -> bytes:
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

    def cmd(c: str, timeout: float = 30.0) -> bytes:
        print(f"\n>>> {c}", flush=True)
        ser.write((c + "\r").encode())
        ser.flush()
        return wait_for(b"=>", timeout=timeout)

    print("=== waiting U-Boot / autoboot ===", flush=True)
    buf = wait_for(
        [b"Hit any key to stop autoboot", b"=>", b"Wrong image format", b"Starting kernel"],
        timeout=120,
    )

    if b"Starting kernel" in buf and b"=>" not in buf[-200:]:
        print("Already booting kernel; cannot intercept", flush=True)
        wait_for([b"login:", b"Kernel panic"], timeout=120)
        ser.close()
        return 3

    # Stop autoboot / get prompt
    for _ in range(10):
        ser.write(b"\r")
        time.sleep(0.05)
    wait_for(b"=>", timeout=5)

    # Prefer sourcing known-good backup script (in-memory; does not rewrite disk).
    for path in [
        "/boot/boot.scr.pre-dual.20260810T131320Z",
        "/boot/stock-1.0.8/boot.scr",
    ]:
        out = cmd(f"load mmc 0:1 ${{scriptaddr}} {path}", timeout=30)
        if b"bytes read" in out:
            print(f"loaded {path}", flush=True)
            print("\n>>> source ${scriptaddr}", flush=True)
            ser.write(b"source ${scriptaddr}\r")
            ser.flush()
            wait_for([b"login:", b"Wrong image format", b"Kernel panic", b"=>"], timeout=180)
            ser.close()
            return 0

    # Fallback: manual stock boot
    for c in [
        'setenv consoleargs "console=ttyS0,115200 console=tty1 earlyprintk=sunxi-uart,0x02500000 initcall_debug=0 splash=verbose"',
        'setenv bootargs "root=UUID=81941f83-3abf-46ef-81f7-cfa8c687d3da rootwait rootfstype=ext4 ${consoleargs} consoleblank=0 loglevel=1 clk_ignore_unused swiotlb=65536 cgroup_enable=cpuset cgroup_memory=1 cgroup_enable=memory swapaccount=1"',
        "load mmc 0:1 ${ramdisk_addr_r} /boot/uInitrd-6.6.98-sun60iw2",
        "load mmc 0:1 ${kernel_addr_r} /boot/uImage",
        "load mmc 0:1 ${fdt_addr_r} /boot/dtb/allwinner/sun60i-a733-orangepi-4-pro.dtb",
        "fdt addr ${fdt_addr_r}",
        "fdt resize 65536",
    ]:
        cmd(c, timeout=60)
    ser.write(b"bootm ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}\r")
    ser.flush()
    wait_for([b"login:", b"Kernel panic"], timeout=180)
    ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
