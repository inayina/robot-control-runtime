# experiments/realtime_userspace — RT3 用户态实时编程夹具

状态：Active（RT3）  
约束：不修改 `linux/` Runtime Core；一次只改变一个变量；失败/缺权限必须可见。

## 解决什么问题

RT1/RT2 已看到同核 `SCHED_OTHER` 压力下尾延迟恶化。RT3 回答：**用户态**还能做哪些可撤销实验来减少不确定性（缺页、优先级反转、周期路径分配/格式化），而不假装已经硬实时。

## 三类实验（退出条件）

| ID | 程序 | 变量 | 观察 |
|---|---|---|---|
| M | `rcr_rt3_mlock` | `mlockall` + 预热 vs 冷触碰 | `/proc/self/status` Minflt/Majflt |
| L | `rcr_rt3_pi_mutex` | `PTHREAD_PRIO_INHERIT` on/off | 高优先级线程等待互斥锁的时间 |
| C | `rcr_rt3_cycle_path` | 周期内 alloc+snprintf vs 预分配；可选忙等过载 | 每次 tick 执行时间 p99/max |

## 构建

```bash
cmake -S experiments/realtime_userspace -B build/rt3 -DCMAKE_BUILD_TYPE=Release
cmake --build build/rt3 -j
ctest --test-dir build/rt3 --output-on-failure
```

## 短跑（本机或 Orange Pi）

```bash
./build/rt3/rcr_rt3_mlock --bytes 16777216
./build/rt3/rcr_rt3_pi_mutex --work-ms 50
# FIFO 权限不足时 result=permission_denied / 退出 77（CTest Skip）
./build/rt3/rcr_rt3_cycle_path --mode prealloc --ticks 2000
./build/rt3/rcr_rt3_cycle_path --mode alloc_format --ticks 2000
./build/rt3/rcr_rt3_cycle_path --mode busy --busy-us 3000 --ticks 200
```

成套证据（写 `evidence/realtime_linux/<stamp>_rt3_userspace/`）：

```bash
./experiments/realtime_userspace/scripts/run_rt3_once.sh
```

## 不能声称

- 实验结果 = 应合并进 Runtime 的默认行为
- mlock/FIFO/PI = 硬实时保证
- ThinkPad 数字 = Orange Pi 结论（须分目录）

## 与备选方案

不把这三类直接写进 `PeriodicScheduler`：当前没有在 clean 矩阵上证明可重复收益，且会把实验开关带进生产路径。独立夹具可随时删除。
