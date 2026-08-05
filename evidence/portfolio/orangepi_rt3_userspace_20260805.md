# RT3 用户态实时编程 — Orange Pi 2026-08-05T113406Z

## Provenance

| field | value |
|---|---|
| run_id | `20260805T113406Z_rt3_userspace` |
| platform | **Orange Pi 4 Pro**（`orangepi4pro` / `6.6.98-sun60iw2`） |
| classification | `experiment`（`git_dirty=true`；`sudo` 跑完整矩阵） |
| 入口 | `experiments/realtime_userspace/`（**未**改 Runtime Core） |
| 前次无 sudo | `20260805T113216Z`（PI 为 permission_denied，已保留） |

ThinkPad 摘要仅作开发对照，见 `rt3_userspace_thinkpad_20260805.md`。

## 板上结果（5/5 pass）

### M — 内存

| 阶段 | delta_minflt |
|---|---:|
| 冷触碰 16 MiB | **4097** |
| `mlockall` 后再触碰 | **0** |

### L — 锁 / PI（同核 CPU0 + FIFO）

| 配置 | high_wait |
|---|---:|
| `PTHREAD_PRIO_INHERIT` | **≈36.8 ms** |
| 无 PI | **≈78.2 ms** |

与 ThinkPad 方向一致：无 PI 时中优先级忙等拉长高优先级等待。

### C — 周期路径（1 ms）

| mode | p99 | max | misses |
|---|---:|---:|---:|
| alloc_format | ≈10.5 µs | ≈29 µs | 0 |
| prealloc | ≈6.1 µs | ≈13 µs | 0 |
| busy 3 ms | ≈3.0 ms | ≈3.0 ms | **200/200** |

## 结论

RT3 退出条件（内存 / 锁·优先级 / 周期路径）在 **Orange Pi** 上已满足。  
未并入 Runtime；不能声称硬实时。

## 复现

```bash
cd ~/robot-control-runtime
sudo bash experiments/realtime_userspace/scripts/rt3_orangepi_once.sh
```
