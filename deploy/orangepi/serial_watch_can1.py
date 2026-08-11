#!/usr/bin/env python3
"""Watch Orange Pi serial during can1 switch; fall back to manual stock boot on failure."""
from __future__ import annotations

import sys
import time

import serial


def main() -> int:
    ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.2)
    print("=== watching for can1 reboot ===", flush=True)
    buf = b""
    t0 = time.time()
    saw_flavor = False
    while time.time() - t0 < 300:
        chunk = ser.read(4096)
        if not chunk:
            time.sleep(0.02)
            continue
        clean = bytes(x for x in chunk if x != 0)
        sys.stdout.buffer.write(clean)
        sys.stdout.buffer.flush()
        buf += chunk
        if b"Selected kernel_flavor=can1" in buf:
            saw_flavor = True
            print("\n[seen] flavor=can1", flush=True)
        if b"Wrong image format" in buf:
            print("\nFAIL_FORMAT", flush=True)
            break
        if b"login:" in buf:
            if saw_flavor or b"6.6.98-sun60iw2-can1" in buf:
                print("\nCAN1_BOOT_OK", flush=True)
                ser.close()
                return 0
            print("\nBOOT_LOGIN (flavor line not seen; check uname over SSH)", flush=True)
            ser.close()
            return 0
        if b"Kernel panic" in buf:
            print("\nPANIC", flush=True)
            ser.close()
            return 3
    else:
        print("\nTIMEOUT", flush=True)
        ser.close()
        return 1

    # Format failure: get prompt and boot stock.
    for _ in range(20):
        ser.write(b"\r")
        time.sleep(0.05)
    # drain to prompt
    t1 = time.time()
    pbuf = b""
    while time.time() - t1 < 5:
        c = ser.read(4096)
        if c:
            sys.stdout.buffer.write(c)
            sys.stdout.buffer.flush()
            pbuf += c
            if b"=>" in pbuf:
                break
    cmds = [
        "setenv consoleargs 'console=ttyS0,115200 console=tty1 earlyprintk=sunxi-uart,0x02500000 initcall_debug=0 splash=verbose'",
        "setenv bootargs 'root=UUID=81941f83-3abf-46ef-81f7-cfa8c687d3da rootwait rootfstype=ext4 ${consoleargs} consoleblank=0 loglevel=1 clk_ignore_unused swiotlb=65536 cgroup_enable=cpuset cgroup_memory=1 cgroup_enable=memory swapaccount=1'",
        "load mmc 0:1 ${ramdisk_addr_r} /boot/uInitrd-6.6.98-sun60iw2",
        "load mmc 0:1 ${kernel_addr_r} /boot/uImage",
        "load mmc 0:1 ${fdt_addr_r} /boot/dtb/allwinner/sun60i-a733-orangepi-4-pro.dtb",
        "fdt addr ${fdt_addr_r}",
        "fdt resize 65536",
    ]

    def cmd(c: str, timeout: float = 90.0) -> None:
        ser.reset_input_buffer()
        print(f"\n>>> {c}", flush=True)
        ser.write((c + "\r").encode())
        ser.flush()
        b = b""
        t = time.time()
        while time.time() - t < timeout:
            x = ser.read(4096)
            if x:
                sys.stdout.buffer.write(bytes(i for i in x if i))
                sys.stdout.buffer.flush()
                b += x
                if b"=>" in b:
                    return
            else:
                time.sleep(0.02)

    for c in cmds:
        cmd(c)
    ser.write(b"bootm ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}\r")
    ser.flush()
    t = time.time()
    while time.time() - t < 240:
        x = ser.read(4096)
        if x:
            sys.stdout.buffer.write(bytes(i for i in x if i))
            sys.stdout.buffer.flush()
            if b"login:" in x or b"login:" in buf:
                print("\nSTOCK_RECOVERY_OK", flush=True)
                ser.close()
                return 4
    ser.close()
    return 5


if __name__ == "__main__":
    raise SystemExit(main())
