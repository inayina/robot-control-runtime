#!/bin/bash
# Dual-boot helpers for Orange Pi 4 Pro CAN kernels.
# Default remains stock. can1=vcan, can2=MCP2515 HAT.
# Never overwrite /boot/uImage (stock).
set -euo pipefail

ENV_FILE=/boot/orangepiEnv.txt
ROOTDEV_UUID=81941f83-3abf-46ef-81f7-cfa8c687d3da

usage() {
  cat <<'EOF'
Usage:
  sudo bash dual_boot_can1.sh install   # install dual boot.scr; keep flavor=stock
  sudo bash dual_boot_can1.sh stock     # kernel_flavor=stock
  sudo bash dual_boot_can1.sh can1      # kernel_flavor=can1 (vcan)
  sudo bash dual_boot_can1.sh can2      # kernel_flavor=can2 + mcp2515-can0 overlay
  sudo bash dual_boot_can1.sh status
EOF
}

require_root() {
  if [[ $(id -u) -ne 0 ]]; then
    echo "need root" >&2
    exit 1
  fi
}

# Preserve overlays/user_overlays/extraargs when rewriting flavor.
write_env() {
  local flavor=$1
  local overlays_line=""
  local user_overlays_line=""
  local extraargs_line=""
  if [[ -f $ENV_FILE ]]; then
    overlays_line=$(grep -E '^overlays=' "$ENV_FILE" || true)
    user_overlays_line=$(grep -E '^user_overlays=' "$ENV_FILE" || true)
    extraargs_line=$(grep -E '^extraargs=' "$ENV_FILE" || true)
  fi
  if [[ $flavor == can2 ]]; then
    overlays_line='overlays=mcp2515-can0'
  elif [[ $flavor == can1 ]]; then
    # can1 software chain does not need MCP overlay; drop stale spi3/mcp lines
    overlays_line=''
  fi
  {
    cat <<EOF
verbosity=1
bootlogo=false
overlay_prefix=sun60i-a733
fdtfile=allwinner/sun60i-a733-orangepi-4-pro.dtb
rootdev=UUID=${ROOTDEV_UUID}
rootfstype=ext4
kernel_flavor=${flavor}
EOF
    [[ -n $overlays_line ]] && echo "$overlays_line"
    [[ -n $user_overlays_line ]] && echo "$user_overlays_line"
    [[ -n $extraargs_line ]] && echo "$extraargs_line"
  } >"$ENV_FILE"
}

status() {
  echo "uname=$(uname -r)"
  echo "uImage hashes:"
  sha256sum /boot/uImage /boot/stock-1.0.8/uImage \
    /boot/uImage-can1 /boot/uImage-can2 2>/dev/null || true
  echo "orangepiEnv:"
  cat "$ENV_FILE" 2>/dev/null || true
  echo "can modules can1:"
  ls /lib/modules/6.6.98-sun60iw2-can1/kernel/drivers/net/can 2>/dev/null || echo missing
  echo "can modules can2:"
  ls /lib/modules/6.6.98-sun60iw2-can2/kernel/drivers/net/can 2>/dev/null || echo missing
  ls /lib/modules/6.6.98-sun60iw2-can2/kernel/drivers/net/can/spi 2>/dev/null || true
  echo "mcp overlay:"
  ls -l /boot/dtb/allwinner/overlay/sun60i-a733-mcp2515-can0.dtbo 2>/dev/null || echo missing
}

install_dual() {
  require_root
  local here
  here=$(cd "$(dirname "$0")" && pwd)
  test -f "$here/boot-sun60iw2-dual.cmd"
  test -f /boot/uImage-can1 || echo "WARN: /boot/uImage-can1 missing"
  test -f /boot/uInitrd-6.6.98-sun60iw2
  command -v mkimage >/dev/null || {
    echo "need mkimage (apt install u-boot-tools)" >&2
    exit 1
  }

  mkimage -C none -A arm -T script \
    -d "$here/boot-sun60iw2-dual.cmd" "$here/boot-sun60iw2-dual.scr"

  cp -a /boot/boot.cmd "/boot/boot.cmd.pre-dual.$(date -u +%Y%m%dT%H%M%SZ)" 2>/dev/null || true
  cp -a /boot/boot.scr "/boot/boot.scr.pre-dual.$(date -u +%Y%m%dT%H%M%SZ)" 2>/dev/null || true
  cp -a "$here/boot-sun60iw2-dual.cmd" /boot/boot.cmd
  cp -a "$here/boot-sun60iw2-dual.scr" /boot/boot.scr

  ln -sfn uInitrd-6.6.98-sun60iw2 /boot/uInitrd
  write_env stock
  [[ -d /lib/modules/6.6.98-sun60iw2-can1 ]] && depmod -a 6.6.98-sun60iw2-can1 || true
  [[ -d /lib/modules/6.6.98-sun60iw2-can2 ]] && depmod -a 6.6.98-sun60iw2-can2 || true
  status
  echo "Installed dual-boot. Default kernel_flavor=stock."
}

case "${1:-}" in
  install) install_dual ;;
  stock) require_root; write_env stock; status ;;
  can1) require_root; write_env can1; status; echo "Next reboot uses can1. Keep USB-TTL ready." ;;
  can2)
    require_root
    test -f /boot/uImage-can2
    test -f /boot/uInitrd-6.6.98-sun60iw2-can2
    test -f /boot/dtb/allwinner/overlay/sun60i-a733-mcp2515-can0.dtbo
    write_env can2
    status
    echo "Next reboot uses can2 + mcp2515-can0. Keep USB-TTL ready."
    ;;
  status) status ;;
  *) usage; exit 2 ;;
esac
