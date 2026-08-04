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

# Orange Pi / 边缘板附加快照：能自动读的写实测值，不能读的写 unavailable。
# 供电、启动介质、镜像来源等必须人工填进 bring-up 勾选表，不在此猜测。
rcr_write_board_snapshot() {
  local out="$1"
  if [[ -e "${out}" ]]; then
    echo "error: refuse overwrite ${out}" >&2
    return 1
  fi
  local dt_model=unavailable
  if [[ -r /proc/device-tree/model ]]; then
    dt_model="$(tr -d '\0' </proc/device-tree/model 2>/dev/null || true)"
    [[ -n "${dt_model}" ]] || dt_model=unavailable
  fi

  local mem_kb
  mem_kb="$(awk '/MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null || true)"
  local mem_mib=unavailable
  if [[ -n "${mem_kb}" ]]; then
    mem_mib="$(awk -v v="${mem_kb}" 'BEGIN { printf "%.0f", v/1024 }')"
  fi

  local systemd_ver=unavailable
  if command -v systemctl >/dev/null 2>&1; then
    systemd_ver="$(systemctl --version 2>/dev/null | head -1 || echo unavailable)"
  fi

  {
    echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "device_tree_model=${dt_model}"
    echo "uname_srm=$(uname -srm)"
    echo "machine=$(uname -m)"
    echo "mem_total_mib=${mem_mib}"
    echo "nproc=$(nproc 2>/dev/null || echo unavailable)"
    echo "systemd_version=${systemd_ver}"
    echo "governor=$(rcr_governor)"
    echo "temp_c=$(rcr_temp_c)"
    echo "power_supply_observed=NOT_OBSERVED"
    echo "boot_media_observed=NOT_OBSERVED"
    echo "os_image_source_observed=NOT_OBSERVED"
    echo "cpu_big_little_map_observed=NOT_OBSERVED"
    echo "undervolt_throttle_observed=NOT_OBSERVED"
    echo "notes=auto fields from host; fill NOT_OBSERVED manually in bring-up checklist"
  } >"${out}"
}
