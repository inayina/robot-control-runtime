#!/usr/bin/env bash
# 将构建产物安装到冻结的 release 布局。
# 默认 dry-run：只打印将要执行的动作，不写磁盘。
# 正式报告式产物先写临时文件再 rename，避免留下半截 MANIFEST。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PREFIX="/opt/robot-control-runtime"
BUILD_DIR="${ROOT}/build/linux"
BUILD_TYPE=""
RELEASE_ID=""
APPLY=0
ACTIVATE=0
CXX_BIN="${CXX:-c++}"

usage() {
  cat <<EOF
usage: $0 [options]

Install built binaries into PREFIX/releases/<release-id>/ with MANIFEST.txt.
Default mode is dry-run (prints planned actions only).

Options:
  --apply              actually write files (still refuses overwrite)
  --activate           after install, point PREFIX/current at this release
  --prefix PATH        absolute install root (default: ${PREFIX})
  --build-dir PATH     cmake build dir (default: ${BUILD_DIR})
  --build-type TYPE    Override recorded build type (else read CMakeCache)
  --release-id ID      override id (default: git short SHA); must match [0-9a-f]{7,40}
  --cxx PATH           compiler for manifest version string
  -h, --help           show this help

Boundary rules:
  - PREFIX must be absolute
  - release directory must not already exist
  - does not delete unknown files; does not install into /usr or source tree
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

# 拒绝把 release 装到源码树或明显错误的前缀，降低误伤风险。
assert_prefix_boundary() {
  local prefix="$1"
  is_absolute "${prefix}" || die "prefix must be absolute: ${prefix}"
  case "${prefix}" in
    /opt/robot-control-runtime|/opt/robot-control-runtime/*) ;;
    /tmp/*|/var/tmp/*) ;; # 允许本机合同自测
    *)
      # 仍允许显式绝对路径，但禁止指向本仓库。
      ;;
  esac
  local prefix_real root_real
  prefix_real="$(realpath -m "${prefix}")"
  root_real="$(realpath -m "${ROOT}")"
  case "${prefix_real}/" in
    "${root_real}/"*) die "refuse install inside source tree: ${prefix}" ;;
  esac
}

read_build_type() {
  local cache="${BUILD_DIR}/CMakeCache.txt"
  if [[ -n "${BUILD_TYPE}" ]]; then
    echo "${BUILD_TYPE}"
    return
  fi
  if [[ -f "${cache}" ]]; then
    local value
    value="$(grep -E '^CMAKE_BUILD_TYPE:' "${cache}" | head -1 | cut -d= -f2- || true)"
    if [[ -n "${value}" ]]; then
      echo "${value}"
      return
    fi
  fi
  echo "unknown"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) APPLY=1; shift ;;
    --activate) ACTIVATE=1; shift ;;
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix needs a value"
      PREFIX="$2"
      shift 2
      ;;
    --build-dir)
      [[ $# -ge 2 ]] || die "--build-dir needs a value"
      BUILD_DIR="$2"
      shift 2
      ;;
    --build-type)
      [[ $# -ge 2 ]] || die "--build-type needs a value"
      BUILD_TYPE="$2"
      shift 2
      ;;
    --release-id)
      [[ $# -ge 2 ]] || die "--release-id needs a value"
      RELEASE_ID="$2"
      shift 2
      ;;
    --cxx)
      [[ $# -ge 2 ]] || die "--cxx needs a value"
      CXX_BIN="$2"
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
done

assert_prefix_boundary "${PREFIX}"
is_absolute "${BUILD_DIR}" || BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"
[[ -d "${BUILD_DIR}" ]] || die "build dir missing: ${BUILD_DIR}"

COMMIT="$(git -C "${ROOT}" rev-parse HEAD)" || die "cannot read git commit"
SHORT="$(git -C "${ROOT}" rev-parse --short=12 HEAD)" || die "cannot read short sha"
if [[ -z "${RELEASE_ID}" ]]; then
  RELEASE_ID="${SHORT}"
fi
[[ "${RELEASE_ID}" =~ ^[0-9a-f]{7,40}$ ]] || die "invalid release id: ${RELEASE_ID}"

if [[ -n "$(git -C "${ROOT}" status --porcelain 2>/dev/null)" ]]; then
  DIRTY=true
else
  DIRTY=false
fi

RESOLVED_BUILD_TYPE="$(read_build_type)"
COMPILER_VERSION="$("${CXX_BIN}" --version | head -1)"
MACHINE="$(uname -m)"
DATE_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

RELEASE_DIR="${PREFIX}/releases/${RELEASE_ID}"
BIN_DIR="${RELEASE_DIR}/bin"
MANIFEST="${RELEASE_DIR}/MANIFEST.txt"
CURRENT_LINK="${PREFIX}/current"

BINARIES=(rcrd rcr_node_sim rcr_benchmark)
SCRIPTS=("${ROOT}/linux/scripts/setup_vcan.sh")

for name in "${BINARIES[@]}"; do
  [[ -x "${BUILD_DIR}/${name}" ]] || die "missing executable: ${BUILD_DIR}/${name}"
done
for script in "${SCRIPTS[@]}"; do
  [[ -f "${script}" ]] || die "missing script: ${script}"
done

if [[ -e "${RELEASE_DIR}" ]]; then
  die "refuse overwrite existing release: ${RELEASE_DIR}"
fi

echo "release_id=${RELEASE_ID}"
echo "git_commit=${COMMIT}"
echo "git_dirty=${DIRTY}"
echo "prefix=${PREFIX}"
echo "build_dir=${BUILD_DIR}"
echo "build_type=${RESOLVED_BUILD_TYPE}"

log "mkdir -p ${BIN_DIR}"
if [[ "${APPLY}" -eq 1 ]]; then
  mkdir -p "${BIN_DIR}"
fi

declare -A SHA256_MAP=()

install_file() {
  local src="$1"
  local dst="$2"
  local mode="$3"
  log "install -m ${mode} ${src} -> ${dst}"
  if [[ "${APPLY}" -eq 1 ]]; then
    install -m "${mode}" "${src}" "${dst}"
  fi
  if [[ "${APPLY}" -eq 1 ]]; then
    SHA256_MAP["$(basename "${dst}")"]="$(sha256sum "${dst}" | awk '{print $1}')"
  else
    SHA256_MAP["$(basename "${dst}")"]="$(sha256sum "${src}" | awk '{print $1}')"
  fi
}

for name in "${BINARIES[@]}"; do
  install_file "${BUILD_DIR}/${name}" "${BIN_DIR}/${name}" 0755
done
install_file "${ROOT}/linux/scripts/setup_vcan.sh" "${BIN_DIR}/setup_vcan.sh" 0755

# MANIFEST 先写临时文件再原子替换，避免失败留下看似有效的空合同。
write_manifest() {
  local out="$1"
  local tmp="${out}.tmp.$$"
  {
    echo "date_utc=${DATE_UTC}"
    echo "hostname=$(hostname)"
    echo "machine=${MACHINE}"
    echo "compiler=${COMPILER_VERSION}"
    echo "build_type=${RESOLVED_BUILD_TYPE}"
    echo "build_dir=${BUILD_DIR}"
    echo "git_commit=${COMMIT}"
    echo "git_short=${SHORT}"
    echo "git_dirty=${DIRTY}"
    echo "release_id=${RELEASE_ID}"
    echo "prefix=${PREFIX}"
    echo "sha256_rcrd=${SHA256_MAP[rcrd]}"
    echo "sha256_rcr_node_sim=${SHA256_MAP[rcr_node_sim]}"
    echo "sha256_rcr_benchmark=${SHA256_MAP[rcr_benchmark]}"
    echo "sha256_setup_vcan_sh=${SHA256_MAP[setup_vcan.sh]}"
  } >"${tmp}"
  mv -f "${tmp}" "${out}"
}

log "write ${MANIFEST}"
if [[ "${APPLY}" -eq 1 ]]; then
  write_manifest "${MANIFEST}"
  chmod 0644 "${MANIFEST}"
else
  echo "----- manifest preview -----"
  echo "date_utc=${DATE_UTC}"
  echo "git_commit=${COMMIT}"
  echo "git_dirty=${DIRTY}"
  echo "release_id=${RELEASE_ID}"
  echo "sha256_rcrd=${SHA256_MAP[rcrd]}"
  echo "sha256_rcr_node_sim=${SHA256_MAP[rcr_node_sim]}"
  echo "sha256_rcr_benchmark=${SHA256_MAP[rcr_benchmark]}"
  echo "sha256_setup_vcan_sh=${SHA256_MAP[setup_vcan.sh]}"
fi

if [[ "${ACTIVATE}" -eq 1 ]]; then
  log "ln -sfn releases/${RELEASE_ID} ${CURRENT_LINK}"
  if [[ "${APPLY}" -eq 1 ]]; then
    mkdir -p "${PREFIX}"
    ln -sfn "releases/${RELEASE_ID}" "${CURRENT_LINK}"
  fi
else
  echo "note: current not changed (pass --activate to point ${CURRENT_LINK})"
fi

if [[ "${APPLY}" -eq 0 ]]; then
  echo "dry-run complete; re-run with --apply to write ${RELEASE_DIR}"
else
  echo "installed: ${RELEASE_DIR}"
  if [[ "${ACTIVATE}" -eq 1 ]]; then
    echo "current -> $(readlink "${CURRENT_LINK}")"
  fi
fi
