#!/usr/bin/env bash
# 回滚：只切换 current 到已存在的 release，不删除任何文件。
# 默认 dry-run。服务重启仅在 --restart 且本机有 systemctl 时执行。
set -euo pipefail

PREFIX="/opt/robot-control-runtime"
APPLY=0
RESTART=0
LIST=0
TARGET_ID=""
RESTART_UNITS=()

usage() {
  cat <<EOF
usage: $0 [options] [<release-id>]

Point PREFIX/current at an existing releases/<id>. Never deletes releases,
source trees, or evidence.

Options:
  --apply           perform the symlink switch
  --restart         after switch, restart the selected/default service set
  --restart-unit U  add one exact systemd unit to try-restart (repeatable)
  --prefix PATH     absolute install root (default: ${PREFIX})
  --list            list installed releases and current target
  -h, --help        show this help
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

log() {
  if [[ "${APPLY}" -eq 0 ]]; then
    echo "dry-run: $*"
  else
    echo "apply: $*"
  fi
}

is_absolute() {
  [[ "$1" == /* ]]
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) APPLY=1; shift ;;
    --restart) RESTART=1; shift ;;
    --restart-unit)
      [[ $# -ge 2 ]] || die "--restart-unit needs a value"
      RESTART_UNITS+=("$2")
      RESTART=1
      shift 2
      ;;
    --list) LIST=1; shift ;;
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix needs a value"
      PREFIX="$2"
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    -*)
      die "unknown argument: $1"
      ;;
    *)
      [[ -z "${TARGET_ID}" ]] || die "multiple release ids"
      TARGET_ID="$1"
      shift
      ;;
  esac
done

is_absolute "${PREFIX}" || die "prefix must be absolute: ${PREFIX}"
RELEASES="${PREFIX}/releases"
CURRENT_LINK="${PREFIX}/current"

if [[ "${LIST}" -eq 1 ]]; then
  echo "prefix=${PREFIX}"
  if [[ -L "${CURRENT_LINK}" ]]; then
    echo "current=$(readlink "${CURRENT_LINK}")"
  elif [[ -e "${CURRENT_LINK}" ]]; then
    echo "current=<not a symlink>"
  else
    echo "current=<missing>"
  fi
  if [[ -d "${RELEASES}" ]]; then
    echo "releases:"
    # 只列一层目录名，避免把未知文件当 release。
    find "${RELEASES}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
  else
    echo "releases: <missing>"
  fi
  exit 0
fi

[[ -n "${TARGET_ID}" ]] || die "release-id required (or use --list)"
[[ "${TARGET_ID}" =~ ^[0-9a-f]{7,40}$ ]] || die "invalid release id: ${TARGET_ID}"

TARGET_DIR="${RELEASES}/${TARGET_ID}"
[[ -d "${TARGET_DIR}" ]] || die "release not found: ${TARGET_DIR}"
[[ -f "${TARGET_DIR}/MANIFEST.txt" ]] || die "MANIFEST missing: ${TARGET_DIR}/MANIFEST.txt"
[[ -x "${TARGET_DIR}/bin/rcrd" ]] || die "rcrd missing in release: ${TARGET_DIR}"

# 若 current 已指向目标，仍允许幂等切换，但给出说明。
if [[ -L "${CURRENT_LINK}" ]]; then
  prev="$(readlink "${CURRENT_LINK}")"
  echo "previous_current=${prev}"
fi

log "ln -sfn releases/${TARGET_ID} ${CURRENT_LINK}"
if [[ "${APPLY}" -eq 1 ]]; then
  mkdir -p "${PREFIX}"
  ln -sfn "releases/${TARGET_ID}" "${CURRENT_LINK}"
  echo "current -> $(readlink "${CURRENT_LINK}")"
else
  echo "dry-run complete; re-run with --apply to switch current"
fi

if [[ "${RESTART}" -eq 1 ]]; then
  if [[ "${#RESTART_UNITS[@]}" -eq 0 ]]; then
    RESTART_UNITS=(rcrd.service rcr-vcan.service)
  fi
  if [[ "${APPLY}" -eq 0 ]]; then
    printf 'dry-run: would systemctl try-restart %s\n' "${RESTART_UNITS[*]}"
  elif command -v systemctl >/dev/null 2>&1; then
    # 回滚只切 current；重启失败必须返回失败，调用方才能停止而不掩盖新状态。
    for unit in "${RESTART_UNITS[@]}"; do
      systemctl try-restart "${unit}"
    done
  else
    die "--restart requested but systemctl is unavailable"
  fi
fi
