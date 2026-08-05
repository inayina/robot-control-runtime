#!/usr/bin/env bash
# CPU 拓扑只读探测：按 cpuinfo_max_freq 区分 big/little，不硬编码核号。
# 从仓库脚本 source；依赖 /sys/devices/system/cpu。
#
# 导出（调用 rcr_topology_detect 后）：
#   RCR_TOPO_ONLINE          逗号分隔在线 CPU
#   RCR_TOPO_BIG_CPUS        最高频档 CPU 列表（逗号）
#   RCR_TOPO_LITTLE_CPUS     其余在线 CPU（逗号）
#   RCR_TOPO_A76_CPU         选中的一个 big 核（默认列表末项）
#   RCR_TOPO_A55_CPU         选中的一个 little 核（默认列表首项）
#   RCR_TOPO_SUMMARY_LINE    单行摘要

rcr_topology_detect() {
  local online_raw
  online_raw="$(cat /sys/devices/system/cpu/online 2>/dev/null || true)"
  if [[ -z "${online_raw}" ]]; then
    echo "error: cannot read /sys/devices/system/cpu/online" >&2
    return 1
  fi

  local -a online_cpus=()
  local part
  IFS=',' read -ra parts <<<"${online_raw}"
  for part in "${parts[@]}"; do
    if [[ "${part}" == *-* ]]; then
      local start="${part%-*}" end="${part#*-}"
      local i
      for ((i = start; i <= end; i++)); do
        online_cpus+=("${i}")
      done
    else
      online_cpus+=("${part}")
    fi
  done

  local max_freq=0
  local cpu freq
  declare -A freq_of=()
  for cpu in "${online_cpus[@]}"; do
    freq="$(cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/cpuinfo_max_freq" 2>/dev/null || echo 0)"
    freq_of["${cpu}"]="${freq}"
    if [[ "${freq}" -gt "${max_freq}" ]]; then
      max_freq="${freq}"
    fi
  done

  if [[ "${max_freq}" -le 0 ]]; then
    echo "error: no readable cpuinfo_max_freq; refuse to guess big.LITTLE map" >&2
    return 1
  fi

  local -a big=() little=()
  for cpu in "${online_cpus[@]}"; do
    if [[ "${freq_of[${cpu}]}" -eq "${max_freq}" ]]; then
      big+=("${cpu}")
    else
      little+=("${cpu}")
    fi
  done

  if [[ "${#big[@]}" -eq 0 ]]; then
    echo "error: empty big CPU set" >&2
    return 1
  fi
  if [[ "${#little[@]}" -eq 0 ]]; then
    # 同频全核：仍可选两个不同核做对照，但 class 标记为 same_freq。
    little=("${big[@]}")
  fi

  local IFS=,
  export RCR_TOPO_ONLINE="${online_cpus[*]}"
  export RCR_TOPO_BIG_CPUS="${big[*]}"
  export RCR_TOPO_LITTLE_CPUS="${little[*]}"
  # 选末个 big / 首个 little，避免默认绑在 cpu0 小核却误称主测。
  export RCR_TOPO_A76_CPU="${big[$((${#big[@]} - 1))]}"
  export RCR_TOPO_A55_CPU="${little[0]}"
  if [[ "${RCR_TOPO_A76_CPU}" == "${RCR_TOPO_A55_CPU}" && "${#online_cpus[@]}" -gt 1 ]]; then
    export RCR_TOPO_A55_CPU="${online_cpus[0]}"
    if [[ "${RCR_TOPO_A55_CPU}" == "${RCR_TOPO_A76_CPU}" ]]; then
      export RCR_TOPO_A55_CPU="${online_cpus[1]}"
    fi
  fi
  export RCR_TOPO_SUMMARY_LINE="online=${RCR_TOPO_ONLINE}; big(${max_freq}kHz)=${RCR_TOPO_BIG_CPUS}; little=${RCR_TOPO_LITTLE_CPUS}; pick_a76=${RCR_TOPO_A76_CPU}; pick_a55=${RCR_TOPO_A55_CPU}"
}

# 写出可读拓扑文件（供证据目录）。
rcr_topology_write() {
  local out="$1"
  if [[ -e "${out}" ]]; then
    echo "error: refuse overwrite ${out}" >&2
    return 1
  fi
  rcr_topology_detect || return 1
  {
    echo "summary=${RCR_TOPO_SUMMARY_LINE}"
    echo "online=${RCR_TOPO_ONLINE}"
    echo "big_cpus=${RCR_TOPO_BIG_CPUS}"
    echo "little_cpus=${RCR_TOPO_LITTLE_CPUS}"
    echo "selected_a76_cpu=${RCR_TOPO_A76_CPU}"
    echo "selected_a55_cpu=${RCR_TOPO_A55_CPU}"
    echo
    local cpu
    for cpu in ${RCR_TOPO_ONLINE//,/ }; do
      echo "cpu${cpu}_max_freq_khz=$(cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/cpuinfo_max_freq" 2>/dev/null || echo unavailable)"
      echo "cpu${cpu}_governor=$(cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor" 2>/dev/null || echo unavailable)"
      echo "cpu${cpu}_cur_freq_khz=$(cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_cur_freq" 2>/dev/null || echo unavailable)"
    done
    echo
    if command -v lscpu >/dev/null 2>&1; then
      echo "lscpu:"
      lscpu
    fi
  } >"${out}"
}

# 逗号列表去掉某一个 CPU，得到 other-core 集合。若结果为空则失败。
rcr_topology_others() {
  local exclude="$1"
  local -a others=()
  local cpu
  for cpu in ${RCR_TOPO_ONLINE//,/ }; do
    if [[ "${cpu}" != "${exclude}" ]]; then
      others+=("${cpu}")
    fi
  done
  if [[ "${#others[@]}" -eq 0 ]]; then
    echo "error: no other CPUs besides ${exclude}" >&2
    return 1
  fi
  local IFS=,
  echo "${others[*]}"
}
