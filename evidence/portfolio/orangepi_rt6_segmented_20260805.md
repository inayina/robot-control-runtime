# RT6 分段时延 — 2026-08-05（Orange Pi）

## Verdict

**4/4 pass**（baseline / cb_busy / io_busy / compare self-check）。  
证据：`evidence/realtime_linux/20260805T115347Z_rt6_segmented/`（板上 Release，dirty experiment）。

软件 peer（`PeriodicScheduler` + `eventfd`/`epoll`），**不是** CAN 端到端，**未**并入 Runtime Core。

## Setup

| 字段 | 值 |
|---|---|
| Host | `orangepi4pro` / `6.6.98-sun60iw2` |
| Period / ticks | 1000 µs / 2000 |
| Busy | 500 µs（对照格） |
| FIFO | 0（OTHER） |
| Temp | 54.4 → 51.3 °C |

## Segment highlights（ns）

| 模式 | 段 | p50 | p99 |
|---|---|---:|---:|
| baseline | wakeup | 60 292 | 88 729 |
| baseline | callback | 250 | 459 |
| baseline | queue | 34 418 | 124 002 |
| baseline | io_ack | 375 | 500 |
| baseline | e2e | 96 475 | 195 499 |
| cb_busy | callback | **500 508** | 500 674 |
| io_busy | io_ack | **500 382** | 501 007 |

机制审阅：callback 忙等抬高 `callback_*`；I/O 忙等抬高 `io_ack_*`；`drops=0`；`deadline_misses=0`（本窗）。

## 不能声称

- 软件 peer e2e = 控制/CAN 延迟  
- 硬实时或 PREEMPT_RT 收益  
- 应默认合并进 `rcrd`
