# LD5 Local Incident Drills

本目录保存 `linux/scripts/run_ld5_incidents.sh` 产生的本机事故演练原始输出。
时间戳子目录通常受仓库 evidence 忽略规则保护，不能当作已提交的 clean release evidence。
RCA 叙述保留在 [`docs/incidents/`](../../docs/incidents/)，不与原始 stdout、样本和退出码混放。

分类固定为 `LOCAL / VCAN / LOOPBACK / DIRTY`：

- 不操作 host systemd unit、物理 `can0`、`/dev/ttyS7` 或 STM32；
- `test_modbus_agent_loopback` 的 mock RTU 和 TCP loopback 不等于物理 RS-485；
- benchmark 只观察普通 Linux 周期唤醒/deadline miss，不证明硬实时或 CAN 端到端延迟；
- live systemd restart、interface down、ptrace attach、network namespace 本轮为 `NOT_RUN`，不绕过权限。

入口：

```bash
RCR_BUILD_DIR=build/ld2-qt-off ./linux/scripts/run_ld5_incidents.sh
```

脚本拒绝覆盖同一 UTC 时间戳目录，并只清理自己记录的子进程 PID。

最近一次修复后的本机批次为 `20260818T141251Z`：五场景 `pass`，但仍是 dirty local evidence，
不能据此关闭 Orange Pi 或 physical Gate。
