#!/usr/bin/env bash
# Orange Pi P3-B2 one-shot installer. Run on the board as root:
#   sudo bash /home/orangepi/robot-control-runtime/deploy/orangepi/b2_bringup_once.sh
#
# On kernels without CONFIG_CAN (current Orange Pi vendor image), rcr-vcan and
# therefore rcrd will fail to become active. The script still installs release,
# user, and units, and records the failure for evidence.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${HOME}/b2_bringup_report.txt"
if [[ "${EUID}" -eq 0 && -n "${SUDO_USER:-}" ]]; then
  OUT="$(getent passwd "${SUDO_USER}" | cut -d: -f6)/b2_bringup_report.txt"
fi

exec > >(tee "${OUT}") 2>&1

echo "=== B2 bring-up $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
echo "root=${ROOT}"
echo "kernel=$(uname -r)"
grep CONFIG_CAN "/boot/config-$(uname -r)" 2>/dev/null || true
systemctl --version | head -1

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo bash $0" >&2
  exit 1
fi

if ! getent passwd rcr >/dev/null; then
  useradd --system --home /nonexistent --shell /usr/sbin/nologin --user-group rcr
  echo "created user rcr"
else
  echo "user rcr already exists"
fi
getent passwd rcr
getent group rcr

"${ROOT}/deploy/orangepi/install_release.sh" --apply --activate --build-dir "${ROOT}/build/linux"

install -d -m 0755 /etc/robot-control-runtime
install -m 0644 "${ROOT}/deploy/systemd/rcr-vcan.service" /etc/systemd/system/
install -m 0644 "${ROOT}/deploy/systemd/rcrd.service" /etc/systemd/system/
install -m 0644 "${ROOT}/deploy/systemd/rcr-node-sim.service" /etc/systemd/system/
systemctl daemon-reload

systemctl enable rcr-vcan.service rcrd.service
systemctl disable rcr-node-sim.service 2>/dev/null || true

echo "=== enable state ==="
systemctl is-enabled rcr-vcan.service rcrd.service rcr-node-sim.service || true

echo "=== start rcr-vcan (expect FAIL if CONFIG_CAN unset) ==="
set +e
systemctl start rcr-vcan.service
echo "rcr-vcan_start_exit=$?"
systemctl start rcrd.service
echo "rcrd_start_exit=$?"
set -e

echo "=== is-active ==="
systemctl is-active rcr-vcan.service rcrd.service || true
systemctl --no-pager --full status rcr-vcan.service rcrd.service || true

echo "=== journal rcr-vcan ==="
journalctl -u rcr-vcan -b --no-pager -n 40 || true
echo "=== journal rcrd ==="
journalctl -u rcrd -b --no-pager -n 40 || true

echo "=== paths ==="
readlink -f /opt/robot-control-runtime/current || true
ls -l /opt/robot-control-runtime/current/bin/ || true
cat /opt/robot-control-runtime/current/MANIFEST.txt || true
sha256sum /opt/robot-control-runtime/current/bin/rcrd || true
systemctl cat rcrd.service || true
systemctl show rcrd -p LimitRTPRIO -p User -p FragmentPath -p Requires -p After || true

echo "=== B2 script done; report: ${OUT} ==="
