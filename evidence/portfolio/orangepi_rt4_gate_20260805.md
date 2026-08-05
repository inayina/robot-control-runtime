# RT4 PREEMPT_RT Gate — 2026-08-05

## Verdict

**Blocked**（Orange Pi 禁止安装/覆盖 RT 内核）+ **Fallback**（ThinkPad 可做方法对照）。

完整分析见 [`docs/PREEMPT_RT_FEASIBILITY_GATE.md`](../../docs/PREEMPT_RT_FEASIBILITY_GATE.md)。  
只读探针：`evidence/realtime_linux/20260805T113914Z_orangepi_rt4_gate/`。

## Why blocked

| 检查 | 结果 |
|---|---|
| 当前是否已是 PREEMPT_RT | 否（`CONFIG_PREEMPT=y`，无 `/sys/kernel/realtime`） |
| 源码↔本机 uImage hash 闭环 | 未关闭 |
| RT 补丁兼容性 | 未知 |
| 双启动项并存 | 无（`boot.cmd` 只载 `uImage`） |
| boot/root 分离 | 否（同为 `mmcblk1p1`） |

## Allowed next

- 保留 RT0–RT3 普通内核证据；  
- ThinkPad 上学习 RT 工具链时不得冒充 Orange Pi RT；  
- 未重开 Gate 前不做 RT5。
