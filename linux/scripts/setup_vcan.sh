#!/usr/bin/env bash
# 为 Orange Pi/Linux 纯软件开发启用虚拟 CAN 接口。
# 需要 root（或 CAP_NET_ADMIN）；该脚本不会配置或访问真实 MCP2515 硬件。
# Runtime 库只做只读探测，不创建接口；本脚本是唯一支持的运维入口。
set -euo pipefail

IFACE="${1:-vcan0}"
ARPHRD_CAN=280

if [[ ! "${IFACE}" =~ ^[a-zA-Z0-9_]{1,15}$ ]]; then
  echo "error: invalid interface name: ${IFACE}" >&2
  exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
  echo "error: run as root, e.g. sudo $0 ${IFACE}" >&2
  exit 1
fi

modprobe vcan || true

if [[ -e "/sys/class/net/${IFACE}" ]]; then
  type_value="$(cat "/sys/class/net/${IFACE}/type" 2>/dev/null || true)"
  if [[ "${type_value}" != "${ARPHRD_CAN}" ]]; then
    echo "error: ${IFACE} exists but is not a CAN interface (type=${type_value:-unknown})" >&2
    exit 1
  fi
else
  ip link add dev "${IFACE}" type vcan
fi

ip link set dev "${IFACE}" up

type_value="$(cat "/sys/class/net/${IFACE}/type")"
if [[ "${type_value}" != "${ARPHRD_CAN}" ]]; then
  echo "error: ${IFACE} is up but type is ${type_value}, expected ARPHRD_CAN=${ARPHRD_CAN}" >&2
  exit 1
fi

ip -details link show "${IFACE}"
echo "ok: ${IFACE} is up (software vcan only)"
