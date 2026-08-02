#!/usr/bin/env bash
# 采集公共证据环境字段。从仓库根目录 source；缺 git commit 则失败。
# 用法: source linux/scripts/lib/evidence_env.sh && rcr_write_environment FILE build_dir build_type

rcr_git_commit() {
  git -C "${RCR_ROOT}" rev-parse HEAD
}

rcr_git_dirty() {
  if [[ -n "$(git -C "${RCR_ROOT}" status --porcelain 2>/dev/null)" ]]; then
    echo true
  else
    echo false
  fi
}

rcr_cpu_model() {
  local model
  model="$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //')"
  if [[ -z "${model}" ]]; then
    echo unavailable
  else
    echo "${model}"
  fi
}

rcr_governor() {
  local g
  g="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true)"
  if [[ -z "${g}" ]]; then
    echo unavailable
  else
    echo "${g}"
  fi
}

rcr_temp_c() {
  local t
  t="$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || true)"
  if [[ -z "${t}" ]]; then
    echo unavailable
  else
    # 通常为毫摄氏度
    awk -v v="${t}" 'BEGIN { printf "%.1f", v/1000.0 }'
  fi
}

rcr_stress_ng_status() {
  if command -v stress-ng >/dev/null 2>&1; then
    echo present
  else
    echo missing
  fi
}

rcr_write_environment() {
  local out="$1"
  local build_dir="${2:-}"
  local build_type="${3:-}"
  if [[ -e "${out}" ]]; then
    echo "error: refuse overwrite ${out}" >&2
    return 1
  fi
  local commit
  commit="$(rcr_git_commit)" || {
    echo "error: cannot read git commit from ${RCR_ROOT}" >&2
    return 1
  }
  {
    echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "hostname=$(hostname)"
    echo "machine=$(uname -m)"
    echo "cpu_model=$(rcr_cpu_model)"
    echo "os_kernel=$(uname -srm)"
    echo "compiler=$(${CXX:-c++} --version | head -1)"
    echo "build_type=${build_type}"
    echo "build_dir=${build_dir}"
    echo "git_commit=${commit}"
    echo "git_dirty=$(rcr_git_dirty)"
    echo "governor=$(rcr_governor)"
    echo "temp_c=$(rcr_temp_c)"
    echo "stress_ng=$(rcr_stress_ng_status)"
  } >"${out}"
}
