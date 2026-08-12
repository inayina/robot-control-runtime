#!/bin/bash
# Install can2 (MCP2515) kernel files WITHOUT overwriting stock /boot/uImage.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ $(id -u) -ne 0 ]]; then echo "need root"; exit 1; fi

test -f "$ROOT/boot/uImage-can2"
test -d "$ROOT/lib/modules/6.6.98-sun60iw2-can2"
test -f /boot/uImage

# Keep a stock snapshot if missing (never treat can* as stock).
if [[ ! -f /boot/stock-1.0.8/uImage ]]; then
  mkdir -p /boot/stock-1.0.8
  cp -a /boot/uImage /boot/stock-1.0.8/
  cp -a /boot/uInitrd-6.6.98-sun60iw2 /boot/stock-1.0.8/ 2>/dev/null || true
  cp -a /boot/orangepiEnv.txt /boot/boot.cmd /boot/boot.scr /boot/stock-1.0.8/ 2>/dev/null || true
fi

cp -a "$ROOT/boot/uImage-can2" /boot/uImage-can2
cp -a "$ROOT/boot/config-6.6.98-sun60iw2-can2" /boot/ 2>/dev/null || true

mkdir -p /lib/modules
rsync -a --delete "$ROOT/lib/modules/6.6.98-sun60iw2-can2/" /lib/modules/6.6.98-sun60iw2-can2/
depmod -a 6.6.98-sun60iw2-can2

if [[ -f $ROOT/boot/dtb/allwinner/overlay/sun60i-a733-mcp2515-can0.dtbo ]]; then
  mkdir -p /boot/dtb/allwinner/overlay
  cp -a "$ROOT/boot/dtb/allwinner/overlay/sun60i-a733-mcp2515-can0.dtbo" \
    /boot/dtb/allwinner/overlay/
fi

if command -v update-initramfs >/dev/null 2>&1; then
  update-initramfs -c -k 6.6.98-sun60iw2-can2 || update-initramfs -u -k 6.6.98-sun60iw2-can2 || true
fi

# Prefer the dual-boot initrd name if update-initramfs wrote a generic path.
if [[ -f /boot/initrd.img-6.6.98-sun60iw2-can2 && ! -f /boot/uInitrd-6.6.98-sun60iw2-can2 ]]; then
  # Match stock packaging: u-boot expects uInitrd-* (uImage wrapper).
  if command -v mkimage >/dev/null; then
    mkimage -A arm -T ramdisk -C none -n uInitrd \
      -d /boot/initrd.img-6.6.98-sun60iw2-can2 /boot/uInitrd-6.6.98-sun60iw2-can2
  else
    echo "WARN: mkimage missing; copy initrd as fallback name"
    cp -a /boot/initrd.img-6.6.98-sun60iw2-can2 /boot/uInitrd-6.6.98-sun60iw2-can2
  fi
fi

echo "Installed can2 side-by-side:"
echo "  /boot/uImage-can2"
echo "  /lib/modules/6.6.98-sun60iw2-can2 (includes mcp251x.ko)"
echo "Stock /boot/uImage NOT modified."
echo "Next: sudo bash deploy/orangepi/dual_boot_can1.sh install"
echo "Then:  sudo bash deploy/orangepi/dual_boot_can1.sh can2 && sudo reboot"
