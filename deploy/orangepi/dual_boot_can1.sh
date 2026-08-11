#!/bin/bash
# Dual-boot helpers for Orange Pi 4 Pro CAN kernel (档1).
# Default remains stock. can1 is opt-in via orangepiEnv kernel_flavor=.
set -euo pipefail

ENV_FILE=/boot/orangepiEnv.txt
STOCK_ENV_KEYS='verbosity bootlogo overlay_prefix fdtfile rootdev rootfstype'

usage() {
  cat <<'EOF'
Usage:
  sudo bash dual_boot_can1.sh install   # install dual boot.scr + keep flavor=stock
  sudo bash dual_boot_can1.sh stock     # set kernel_flavor=stock (safe default)
  sudo bash dual_boot_can1.sh can1      # set kernel_flavor=can1 (needs serial ready)
  sudo bash dual_boot_can1.sh status
EOF
}

require_root() {
  if [[ $(id -u) -ne 0 ]]; then
    echo "need root" >&2
    exit 1
  fi
}

write_env() {
  local flavor=$1
  cat >"$ENV_FILE" <<EOF
verbosity=1
bootlogo=false
overlay_prefix=sun60i-a733
fdtfile=allwinner/sun60i-a733-orangepi-4-pro.dtb
rootdev=UUID=81941f83-3abf-46ef-81f7-cfa8c687d3da
rootfstype=ext4
kernel_flavor=${flavor}
EOF
}

status() {
  echo "uname=$(uname -r)"
  echo "uImage hashes:"
  sha256sum /boot/uImage /boot/stock-1.0.8/uImage /boot/uImage-can1 2>/dev/null || true
  echo "uInitrd -> $(readlink -f /boot/uInitrd 2>/dev/null || true)"
  echo "orangepiEnv:"
  cat "$ENV_FILE" 2>/dev/null || true
  echo "can modules:"
  ls /lib/modules/6.6.98-sun60iw2-can1/kernel/drivers/net/can 2>/dev/null || echo missing
}

install_dual() {
  require_root
  local here
  here=$(cd "$(dirname "$0")" && pwd)
  test -f "$here/boot-sun60iw2-dual.cmd"
  test -f /boot/uImage-can1
  test -f /boot/uInitrd-6.6.98-sun60iw2
  test -f /boot/uInitrd-6.6.98-sun60iw2-can1
  test -d /lib/modules/6.6.98-sun60iw2-can1
  command -v mkimage >/dev/null || {
    echo "need mkimage (apt install u-boot-tools)" >&2
    exit 1
  }

  # Always rebuild .scr on-board. Host-built images once failed with
  # "Wrong image format for source command" on this U-Boot.
  mkimage -C none -A arm -T script \
    -d "$here/boot-sun60iw2-dual.cmd" "$here/boot-sun60iw2-dual.scr"

  # Never overwrite stock default kernel image during install.
  cp -a /boot/boot.cmd "/boot/boot.cmd.pre-dual.$(date -u +%Y%m%dT%H%M%SZ)" 2>/dev/null || true
  cp -a /boot/boot.scr "/boot/boot.scr.pre-dual.$(date -u +%Y%m%dT%H%M%SZ)" 2>/dev/null || true
  cp -a "$here/boot-sun60iw2-dual.cmd" /boot/boot.cmd
  cp -a "$here/boot-sun60iw2-dual.scr" /boot/boot.scr

  # Default initrd symlink back to stock; dual script loads explicit names anyway.
  ln -sfn uInitrd-6.6.98-sun60iw2 /boot/uInitrd
  write_env stock
  depmod -a 6.6.98-sun60iw2-can1 || true
  status
  echo "Installed dual-boot. Default kernel_flavor=stock. Reboot stays on stock."
  echo "Do NOT run: sudo bash dual_boot_can1.sh can1  until USB-TTL is attached."
}

case "${1:-}" in
  install) install_dual ;;
  stock) require_root; write_env stock; status ;;
  can1) require_root; write_env can1; status; echo "Next reboot uses can1. Keep USB-TTL ready." ;;
  status) status ;;
  *) usage; exit 2 ;;
esac
