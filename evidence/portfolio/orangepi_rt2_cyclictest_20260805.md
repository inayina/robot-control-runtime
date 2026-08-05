# RT2 cyclictest 对照与归因 — 2026-08-05T111247Z

## Provenance

| field | value |
|---|---|
| run_id | `20260805T111247Z_orangepi_rt2_cyclictest` |
| classification | `experiment`（`git_dirty=true`） |
| tool | `cyclictest` V2.20 |
| CPU | A76 `cpu7`；governor `performance`（结束后已恢复 ondemand） |
| duration / interval | 60 s / 1000 µs |
| 对照 | RT1 smoke `20260805T103150Z_orangepi_rt1_smoke` |
| 缺失工具 | `timerlat`/`osnoise`/`rtla` missing；`perf` 与 `6.6.98-sun60iw2` 不匹配；ftrace tracers unavailable |

本地元数据：`evidence/realtime_linux/20260805T111247Z_orangepi_rt2_cyclictest/`  
（完整 histogram 体积大，仅保留 header；统计来自 `# Min/Avg/Max Latencies`，单位 **ns**。）

## 四代表条件（cyclictest）

| cell | policy | load | min | avg | max | hist overflows |
|---|---|---|---:|---:|---:|---:|
| 01 | OTHER | idle | 3.6 µs | 53 µs | **3.8 ms** | 59714 / ~60k |
| 02 | OTHER | **same-stress** | 21 µs | **1.28 ms** | **10.0 ms** | 30784（周期未跑满） |
| 03 | FIFO10 | idle | 2.1 µs | 2.8 µs | 24 µs | 28 |
| 04 | FIFO10 | **same-stress** | 2.3 µs | 4.6 µs | 12 µs | 16 |

## 与 RT1 `rcr_benchmark` 对照

| 条件 | RT1 wakeup lateness | cyclictest latency | 方向是否一致 |
|---|---|---|---|
| OTHER idle | p99 54 µs / max 4.5 ms | avg 53 µs / max 3.8 ms | 是 |
| OTHER same-stress | **miss≈43k** / p99 4.0 ms / max 28 ms | avg 1.3 ms / max 10 ms；大量 overflow | 是（最恶劣） |
| FIFO idle | 0 miss / p99 3.6 µs / max 89 µs | avg 2.8 µs / max 24 µs | 是 |
| FIFO same-stress | 0 miss / p99 9.8 µs / max 19 µs | avg 4.6 µs / max 12 µs | 是 |

绝对数字不必相等（工具实现、`-h 10000`+`-N` 直方图窗过窄、采样集合不同），但 **同核压力下 OTHER 崩、FIFO 稳** 两边都成立。

## 归因结论（一个尾延迟尖峰）

**尖峰条件**：A76 + performance + **SCHED_OTHER** + **同核 CPU stress**。

**最可能原因**：同核上 CFS/`SCHED_OTHER` 与 `stress-ng` 公平共享 CPU，周期线程被同优先级（普通）计算负载长时间推迟。

**排除 / 弱化的假设**：

| 假设 | 判定 | 依据 |
|---|---|---|
| 空 callback / 本仓应用逻辑 | 排除为主因 | cyclictest 无本仓 callback，同条件仍差 |
| 纯 IRQ 延迟 | 弱化 | 同核同 stress 下 FIFO 仍 µs 级；若主要是 IRQ，FIFO 也应明显变差 |
| DVFS | 弱化 | 四格均 `performance`，freq 记录约 2002 MHz |
| 缺页（page fault） | 未直接测 | cyclictest 使用 `-m` mlockall；未跑 perf/page-fault 计数（perf unsupported） |
| 热节流 | 可能叠加但非主因 | 本 run 47→65°C，低于 RT1 smoke 的 88°C；FIFO 同温区仍好 |

**未能完成的分解**：无 `timerlat`/`osnoise`，不能把残余抖动拆成 IRQ latency vs thread latency；`perf sched` 不可用。已记 `unsupported`，不假装归因到 IRQ 子类。

## 方法学备注

本 run 使用 `cyclictest -N -h 10000`：在 ns 单位下直方图上限仅 10 µs，导致 OTHER 格大量 overflow。Min/Avg/Max 注释仍覆盖全样本，可用；后续应加大 `-h` 或不用过窄 hist 窗。脚本将改为解析 histogram 注释行。

## 不能声称

硬实时、PREEMPT_RT 收益、IRQ 精确占比、clean-commit 正式基线。
