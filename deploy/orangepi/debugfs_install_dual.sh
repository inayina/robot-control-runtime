#!/bin/bash
# Install dual-boot files into /boot using debugfs (needs disk group, not root).
# WARNING: writes a mounted ext4 volume; always sync and reboot right after.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
DEV=${DEV:-/dev/mmcblk1p1}
FLAVOR=${1:-stock}

need() { test -e "$1" || { echo "missing $1" >&2; exit 1; }; }
need "$HERE/boot-sun60iw2-dual.scr"
need "$HERE/boot-sun60iw2-dual.cmd"
need "$HERE/orangepiEnv.$FLAVOR"
need /boot/uImage-can1
need /boot/uInitrd-6.6.98-sun60iw2
need /boot/uInitrd-6.6.98-sun60iw2-can1

TS=$(date -u +%Y%m%dT%H%M%SZ)
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

cp -a "$HERE/boot-sun60iw2-dual.scr" "$WORKDIR/boot.scr"
cp -a "$HERE/boot-sun60iw2-dual.cmd" "$WORKDIR/boot.cmd"
cp -a "$HERE/orangepiEnv.$FLAVOR" "$WORKDIR/orangepiEnv.txt"

test -w "$DEV" || { echo "cannot write $DEV (need disk group)" >&2; exit 1; }

CMDS=$WORKDIR/cmds.txt
{
  echo "cd boot"
  if [[ -r /boot/boot.scr ]]; then
    cp -a /boot/boot.scr "$WORKDIR/boot.scr.bak"
    echo "write $WORKDIR/boot.scr.bak boot.scr.pre-dual.$TS"
  fi
  if [[ -r /boot/boot.cmd ]]; then
    cp -a /boot/boot.cmd "$WORKDIR/boot.cmd.bak"
    echo "write $WORKDIR/boot.cmd.bak boot.cmd.pre-dual.$TS"
  fi
  # Replace boot script + env. Ignore missing targets via separate -f script below.
  echo "rm boot.scr"
  echo "rm boot.cmd"
  echo "rm orangepiEnv.txt"
  echo "write $WORKDIR/boot.scr boot.scr"
  echo "write $WORKDIR/boot.cmd boot.cmd"
  echo "write $WORKDIR/orangepiEnv.txt orangepiEnv.txt"
  # e2fsprogs debugfs: symlink <linkname> <target>
  echo "unlink uInitrd"
  echo "symlink uInitrd uInitrd-6.6.98-sun60iw2"
  echo "quit"
} >"$CMDS"

sync
# debugfs returns non-zero if any command fails (e.g. rm of missing); drive explicitly.
set +e
debugfs -w -f "$CMDS" "$DEV"
rc=$?
set -e
sync

echo "debugfs finished rc=$rc flavor=$FLAVOR"
debugfs -R "cat /boot/orangepiEnv.txt" "$DEV" || true
debugfs -R "stat /boot/boot.scr" "$DEV" || true
debugfs -R "stat /boot/uInitrd" "$DEV" || true
exit 0
