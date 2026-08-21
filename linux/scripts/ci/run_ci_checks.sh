#!/usr/bin/env bash
# LD7 本地可复现 CI 编排：构建、测试、静态检查和 release artifact。
# 这里只调用现有合同；不创建 host vcan、不启停 host systemd、不访问物理设备。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${RCR_CI_BUILD_DIR:-${ROOT}/build/ci-qt-off}"
QT_BUILD_DIR="${RCR_CI_QT_BUILD_DIR:-${ROOT}/build/ci-qt-on}"
ARTIFACT_DIR="${RCR_CI_ARTIFACT_DIR:-${ROOT}/build/ci-artifacts}"
RUN_QT_ON="${RCR_CI_RUN_QT_ON:-0}"
if [[ -n "${CMAKE_GENERATOR:-}" ]]; then
  GENERATOR="${CMAKE_GENERATOR}"
elif command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
else
  GENERATOR="Unix Makefiles"
fi

cleanup() {
  if [[ -n "${RELEASE_PREFIX:-}" ]]; then
    rm -rf "${RELEASE_PREFIX}"
  fi
}
trap cleanup EXIT

mkdir -p "${ARTIFACT_DIR}"

configure_build() {
  local dir="$1"
  local qt="$2"
  cmake -S "${ROOT}/linux" -B "${dir}" -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DRCR_BUILD_TESTS=ON \
    -DRCR_BUILD_QT_DEVICE_WORKBENCH="${qt}"
  cmake --build "${dir}" --parallel
  QT_QPA_PLATFORM=offscreen ctest --test-dir "${dir}" --output-on-failure
}

configure_build "${BUILD_DIR}" OFF

if [[ "${RUN_QT_ON}" == 1 ]]; then
  configure_build "${QT_BUILD_DIR}" ON
fi

python3 "${ROOT}/linux/scripts/diagnostics/tests/test_diagnostics.py"
python3 "${ROOT}/linux/scripts/ci/check_docs.py"
python3 -m compileall -q "${ROOT}/linux/scripts/diagnostics" "${ROOT}/deploy/orangepi"
python3 -m json.tool "${ROOT}/linux/CMakePresets.json" >/dev/null

while IFS= read -r -d '' script; do
  bash -n "${script}"
done < <(find "${ROOT}/linux/scripts" "${ROOT}/deploy" -type f -name '*.sh' -print0)

# 这是静态 unit 合同检查；脚本会把报告写进被忽略的 evidence/systemd，绝不启停 unit。
bash "${ROOT}/deploy/systemd/verify_units.sh"

# 复用 LD2 的临时 prefix/fake-systemd/localhost 合同，不接 host CAN、serial 或 systemd。
bash "${ROOT}/deploy/orangepi/test_operations.sh" "${BUILD_DIR}"

RELEASE_PREFIX="$(mktemp -d /tmp/rcr-ld7-release.XXXXXX)"
RELEASE_ID="$(git -C "${ROOT}" rev-parse --short=12 HEAD)"
bash "${ROOT}/deploy/orangepi/install_release.sh" --apply --activate \
  --prefix "${RELEASE_PREFIX}" --build-dir "${BUILD_DIR}" --release-id "${RELEASE_ID}"

RELEASE_DIR="${RELEASE_PREFIX}/releases/${RELEASE_ID}"
MANIFEST="${RELEASE_DIR}/MANIFEST.txt"
[[ -f "${MANIFEST}" ]] || { echo "error: release manifest missing" >&2; exit 1; }
[[ "$(readlink "${RELEASE_PREFIX}/current")" == "releases/${RELEASE_ID}" ]] || {
  echo "error: current symlink does not point to CI release" >&2
  exit 1
}

ARTIFACT="${ARTIFACT_DIR}/rcr-release-${RELEASE_ID}.tar.gz"
tar -C "${RELEASE_PREFIX}" -czf "${ARTIFACT}" "releases/${RELEASE_ID}"
sha256sum "${ARTIFACT}" >"${ARTIFACT}.sha256"
cp "${MANIFEST}" "${ARTIFACT_DIR}/MANIFEST.txt"

RCR_ARTIFACT="${ARTIFACT}" bash "${ROOT}/linux/scripts/ci/check_provisioning.sh"

{
  echo "git_commit=$(git -C "${ROOT}" rev-parse HEAD)"
  echo "git_dirty=$([[ -n "$(git -C "${ROOT}" status --porcelain)" ]] && echo true || echo false)"
  echo "qt_off_build=${BUILD_DIR}"
  echo "qt_on_run=${RUN_QT_ON}"
  echo "release_id=${RELEASE_ID}"
  echo "artifact=${ARTIFACT}"
  echo "artifact_sha256=$(cut -d' ' -f1 "${ARTIFACT}.sha256")"
  echo "physical_can=NOT_RUN"
  echo "physical_rs485=NOT_RUN"
  echo "host_systemd_apply=NOT_RUN"
} >"${ARTIFACT_DIR}/CI_SUMMARY.txt"

echo "ci_result=pass artifact=${ARTIFACT}"
