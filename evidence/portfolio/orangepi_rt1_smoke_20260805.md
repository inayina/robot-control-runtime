# RT1 60s smoke review — 2026-08-05T103150Z

## Provenance

| field | value |
|---|---|
| run_id | `20260805T103150Z_orangepi_rt1_smoke` |
| classification | `smoke`（`git_dirty=true` + `ALLOW_DIRTY=1`；非正式 baseline） |
| build_type | Release |
| duration_ms | 60000 × 10 cells |
| topology | A76=`cpu7`，A55=`cpu0` |
| governor | 格内 `enforced`；结束后已恢复 `ondemand` |
| temp_c | 55.5 → **88.3**（冒烟窗口升温明显） |
| result | **10/10 pass**；failed/permission_denied/unsupported = 0 |

本地元数据（无大样本）：`evidence/realtime_linux/20260805T103150Z_orangepi_rt1_smoke/`  
板上完整样本仍在同路径（含 `samples.txt`）。

## 机制是否生效

| 机制 | 证据 |
|---|---|
| CPU affinity | 全部格 `affinity_enabled=1`；A76→7，A55→0 |
| SCHED_FIFO | FIFO 格 `fifo_enabled=1`，priority 10 |
| governor | performance/ondemand 格 `governor_contract=enforced` |
| other-core stress | `stress_affinity` 为除被测核外的核列表 |
| same-core stress | `stress_affinity=7`（与被测同核） |

## 代表数字（wakeup lateness，空 callback）

| cell | misses | p99 | max |
|---|---:|---:|---:|
| A76 perf OTHER idle | 28 | 54 µs | 4.5 ms |
| A76 perf OTHER other-stress | 80 | 67 µs | 5.5 ms |
| A76 perf OTHER **same-stress** | **43530** | **4.0 ms** | **28 ms** |
| A76 perf FIFO idle | 0 | 3.6 µs | 89 µs |
| A76 perf FIFO other-stress | 0 | 17 µs | 101 µs |
| A76 perf FIFO same-stress | 0 | 9.8 µs | 19 µs |
| A55 perf OTHER other-stress | 386 | 267 µs | 7.1 ms |
| A55 perf FIFO other-stress | 0 | 42 µs | 437 µs |
| A76 ondemand OTHER other-stress | 67 | 66 µs | 5.8 ms |
| A76 ondemand FIFO other-stress | 0 | 14 µs | 110 µs |

## 审阅结论

- **可以进入 formal**：affinity / FIFO / governor / 同核·异核压力合同已在 60s 窗口验证。
- **不能当正式基线**：dirty tree；且升温到 88°C，formal 须盯温度并保留失败报告。
- **同核 OTHER 压力**是最恶劣格；FIFO 在同条件下 0 miss——这是学习信号，不是硬实时证明。
- formal 前建议：提交 RT0/RT1 → 板上 clean checkout → 再 `sudo bash deploy/orangepi/rt1_formal_once.sh`。

## 不能声称

硬实时、PREEMPT_RT、CAN/控制端到端延迟、clean-commit 可复现基线。
