# experiments/realtime_segmented — RT6 分段时延夹具

状态：Active（RT6）  
约束：不修改 `linux/` Runtime Core / `CanIoLoop`；主证据在 Orange Pi；软件 peer，非 CAN。

## 解决什么问题

RT1 空 callback 的 wakeup p99 **不能**代替控制路径延迟。RT6 在同一 `CLOCK_MONOTONIC`
上拆开：

```text
scheduled wakeup → callback → enqueue/eventfd → I/O dispatch → software peer ACK
```

报告各段 min/p50/p99/max，并与 `e2e`（ACK − scheduled）对照。

## 构建

```bash
cmake -S experiments/realtime_segmented -B build/rt6 -DCMAKE_BUILD_TYPE=Release
cmake --build build/rt6 -j
ctest --test-dir build/rt6 --output-on-failure
```

## 运行

```bash
./build/rt6/rcr_rt6_segments --mode baseline --ticks 2000 --period-us 1000
./build/rt6/rcr_rt6_segments --mode cb_busy --busy-us 500 --ticks 2000
./build/rt6/rcr_rt6_segments --mode io_busy --busy-us 500 --ticks 2000
./build/rt6/rcr_rt6_segments --mode compare --ticks 2000 --busy-us 500 --self-check
```

成套证据：

```bash
./experiments/realtime_segmented/scripts/run_rt6_once.sh
# Orange Pi（推荐）：
sudo bash experiments/realtime_segmented/scripts/rt6_orangepi_once.sh
```

## 段定义

| 段 | 计算 |
|---|---|
| `wakeup` | t_wake − t_sched |
| `callback` | t_cb_end − t_cb_begin（含可选 busy） |
| `queue` | t_io − t_publish |
| `io_ack` | t_ack − t_io（含可选 I/O busy） |
| `e2e` | t_ack − t_sched |

队列满 → `drops` 可见递增，不覆盖未消费样本。

## 不能声称

- 软件 peer e2e = CAN / 电机端到端延迟
- 已并入 Runtime 默认路径
- 硬实时上界
