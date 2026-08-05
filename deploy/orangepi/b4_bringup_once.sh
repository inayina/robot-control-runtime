#!/usr/bin/env bash
# Orange Pi P3-B4 one-shot helpers. Run on the board as root:
#
#   sudo bash deploy/orangepi/b4_bringup_once.sh prepare
#   sudo reboot
#   # after SSH is back:
#   sudo bash deploy/orangepi/b4_bringup_once.sh post-reboot
#   sudo bash deploy/orangepi/b4_bringup_once.sh rollback <existing-release-id>
#
# On vendor kernel (# CONFIG_CAN is not set): rcr-vcan/rcrd will not become active.
# Record UNSUPPORTED — do not claim B4 cold-start PASS for daemon lifecycle.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${RCR_PREFIX:-/opt/robot-control-runtime}"
OWNER="${SUDO_USER:-orangepi}"
MODE="${1:-}"
TARGET_RELEASE_ID="${2:-}"

die() { echo "error: $*" >&2; exit 1; }
[[ "${EUID}" -eq 0 ]] || die "run as root, e.g. sudo bash $0 <prepare|post-reboot|rollback>"
[[ -n "${MODE}" ]] || die "usage: $0 <prepare|post-reboot|rollback> [existing-release-id]"

home="$(getent passwd "${OWNER}" | cut -d: -f6)"
[[ -n "${home}" && "${home}" == /* && "${home}" != "/" ]] || die \
  "cannot resolve a safe home directory for report owner: ${OWNER}"
REPORT_ROOT="${home}/b4_reports"
mkdir -p "${REPORT_ROOT}"
[[ ! -L "${REPORT_ROOT}" ]] || die "report root must not be a symbolic link: ${REPORT_ROOT}"
REPORT_ROOT_REAL="$(realpath -e "${REPORT_ROOT}")"
[[ "${REPORT_ROOT_REAL}" == "${home}/b4_reports" ]] || die \
  "unexpected report root after resolution: ${REPORT_ROOT_REAL}"

case "${MODE}" in
  prepare)
    OUT="${REPORT_ROOT}/prepare_$(date -u +%Y%m%dT%H%M%SZ).txt"
    exec > >(tee "${OUT}") 2>&1
    echo "=== B4 prepare $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    "${ROOT}/deploy/orangepi/rollback_release.sh" --list --prefix "${PREFIX}"
    shopt -s nullglob
    releases=("${PREFIX}"/releases/*)
    real_release_count=0
    for release_dir in "${releases[@]}"; do
      [[ -d "${release_dir}" && -f "${release_dir}/MANIFEST.txt" ]] || continue
      real_release_count=$((real_release_count + 1))
    done
    [[ "${real_release_count}" -ge 2 ]] || die \
      "need two independently installed releases with MANIFEST; refusing to manufacture a fake rollback target"
    echo "current=$(readlink "${PREFIX}/current")"
    systemctl is-enabled rcr-vcan rcrd rcr-node-sim || true
    systemctl is-active rcr-vcan rcrd || true
    grep CONFIG_CAN "/boot/config-$(uname -r)" 2>/dev/null || true
    echo "NEXT: sudo reboot; after SSH run: sudo bash $0 post-reboot"
    echo "report=${OUT}"
    ;;

  post-reboot)
    OUT="${REPORT_ROOT}/post_reboot_$(date -u +%Y%m%dT%H%M%SZ).txt"
    exec > >(tee "${OUT}") 2>&1
    echo "=== B4 post-reboot $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    echo "boot_time=$(uptime -s) kernel=$(uname -r)"
    echo "--- enable ---"
    systemctl is-enabled rcr-vcan.service rcrd.service rcr-node-sim.service || true
    echo "--- active ---"
    systemctl is-active rcr-vcan.service rcrd.service || true
    systemctl --no-pager --full status rcr-vcan.service rcrd.service || true
    echo "--- journal rcr-vcan ---"
    journalctl -u rcr-vcan -b --no-pager -n 40 || true
    echo "--- journal rcrd ---"
    journalctl -u rcrd -b --no-pager -n 40 || true
    echo "--- current ---"
    readlink -f "${PREFIX}/current" || true
    cat "${PREFIX}/current/MANIFEST.txt" || true
    echo "--- B4-02 start attempt / restart limit ---"
    set +e
    systemctl reset-failed rcr-vcan.service rcrd.service 2>/dev/null
    systemctl start rcr-vcan.service
    echo "rcr-vcan_start_exit=$?"
    systemctl start rcrd.service
    echo "rcrd_start_exit=$?"
    sleep 2
    systemctl is-active rcr-vcan.service rcrd.service
    systemctl show rcrd -p NRestarts -p StartLimitBurst -p StartLimitIntervalUSec -p ActiveState -p SubState || true
    if systemctl is-active --quiet rcrd.service; then
      echo "rcrd active; SIGKILL once for restart observation"
      pid="$(systemctl show -p MainPID --value rcrd)"
      [[ "${pid}" =~ ^[0-9]+$ && "${pid}" -gt 1 ]] || die \
        "refusing to signal unexpected MainPID: ${pid}"
      kill -KILL "${pid}" 2>/dev/null || true
      sleep 3
      systemctl show rcrd -p NRestarts -p ActiveState -p SubState || true
    else
      echo "rcrd not active; crash-restart observation unsupported (kernel has no CAN)"
    fi
    set -e
    echo "NEXT: choose a real previous id from --list, then run: sudo bash $0 rollback <release-id>"
    echo "report=${OUT}"
    ;;

  rollback)
    OUT="${REPORT_ROOT}/rollback_$(date -u +%Y%m%dT%H%M%SZ).txt"
    exec > >(tee "${OUT}") 2>&1
    echo "=== B4 rollback $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    "${ROOT}/deploy/orangepi/rollback_release.sh" --list --prefix "${PREFIX}"
    [[ -n "${TARGET_RELEASE_ID}" ]] || die "rollback requires an explicit existing release id"
    [[ "${TARGET_RELEASE_ID}" =~ ^[A-Za-z0-9._-]+$ ]] || die "invalid release id"
    CUR="$(readlink "${PREFIX}/current" | sed 's|.*/||')"
    [[ "${TARGET_RELEASE_ID}" != "${CUR}" ]] || die "target is already current"
    TARGET_DIR="${PREFIX}/releases/${TARGET_RELEASE_ID}"
    [[ -d "${TARGET_DIR}" && -f "${TARGET_DIR}/MANIFEST.txt" ]] || die \
      "target release is missing or has no MANIFEST: ${TARGET_RELEASE_ID}"
    echo "switching current ${CUR} -> ${TARGET_RELEASE_ID}"
    "${ROOT}/deploy/orangepi/rollback_release.sh" --apply --restart --prefix "${PREFIX}" \
      "${TARGET_RELEASE_ID}"
    echo "current_after=$(readlink "${PREFIX}/current")"
    test -f "${PREFIX}/current/MANIFEST.txt"
    echo "sha256_bin=$(sha256sum "${PREFIX}/current/bin/rcrd")"
    grep '^sha256_rcrd=' "${PREFIX}/current/MANIFEST.txt"
    systemctl is-active rcr-vcan rcrd || true
    ls -d "${PREFIX}/releases/${CUR}" >/dev/null
    echo "old_release_kept=${PREFIX}/releases/${CUR}"
    echo "report=${OUT}"
    ;;

  *)
    die "unknown mode: ${MODE} (want prepare|post-reboot|rollback [release-id])"
    ;;
esac

chown -R "${OWNER}:${OWNER}" "${REPORT_ROOT_REAL}"
echo "done mode=${MODE}"
