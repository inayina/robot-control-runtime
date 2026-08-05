# RT3 用户态实时编程夹具 — 2026-08-05T112859Z

## Provenance

| field | value |
|---|---|
| run_id | `20260805T112859Z_rt3_userspace` |
| classification | `experiment`（ThinkPad 本机；dirty 工作树） |
| build | `build/rt3` Release |
| 入口 | `experiments/realtime_userspace/`（**未**改 Runtime Core） |

## 三类结果

### M — 内存（`rcr_rt3_mlock`）

| 阶段 | delta_minflt |
|---|---:|
| 冷触碰 16 MiB 匿名页 | **4097** |
| `mlockall` 后再次触碰 | **0** |

`mlockall_ok=1`。说明：缺页是可观察的用户态抖动来源；锁定+预热可抑制二次触碰缺页。  
`/proc/self/status` 的 Minflt 在本机不可靠，改用 `getrusage.ru_minflt`。

### L — 锁 / 优先级继承（`rcr_rt3_pi_mutex`，同核 CPU0 + FIFO）

| 配置 | high_wait |
|---|---:|
| `PTHREAD_PRIO_INHERIT` | **≈37 ms**（≈持锁工作 40 ms） |
| 无 PI | **≈78 ms**（中优先级忙等拉长等待） |

同核绑定后反转可见；多核不绑定时对照可能消失。FIFO 权限不足时应记 `permission_denied`。

### C — 周期路径（`rcr_rt3_cycle_path`，1 ms × 2000）

| mode | exec p99 | exec max | misses |
|---|---:|---:|---:|
| alloc_format（周期内 new+snprintf） | ≈11 µs | ≈26 µs | 0 |
| prealloc | ≈4 µs | ≈10 µs | 0 |
| busy 3 ms（过载） | ≈3.0 ms | ≈3.0 ms | **200/200** |

预分配更短尾；过载必然 miss 并跳过旧边界（与 `rcr_benchmark --callback-delay-us` 同思路）。

## 取舍

- **未**并入 Runtime：未在 Orange Pi clean 矩阵上证明可重复收益。
- 未做：把日志移出周期线程的完整异步队列；线程关闭顺序专项（可用现有 daemon 测试补充）。
- 不能声称硬实时；不能把本机 PI/mlock 数字写成 Orange Pi 基线。

## 复现（板上）

```bash
cmake -S experiments/realtime_userspace -B build/rt3 -DCMAKE_BUILD_TYPE=Release
cmake --build build/rt3 -j
# 无 FIFO 的格可直接跑；完整含 PI：
sudo bash experiments/realtime_userspace/scripts/rt3_orangepi_once.sh
```

ThinkPad 对照摘要见 `rt3_userspace_thinkpad_20260805.md`（可含 PI 数值，不能替代板上结论）。
