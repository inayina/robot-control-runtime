# CAN + Modbus 类型化观测实验

这个独立实验搭建最小的多通道**观测**路径：CAN V1 `NodeStatus` 是事件流，Modbus TCP
Holding Register 是 100 ms 请求/响应轮询；两者只在类型化快照处汇合，不进入 `rcrd`
控制决策。

```text
vcan0 → SocketCAN → epoll worker → CAN V1 decode → CanStatusSample ┐
                                                                  ├→ ObservationStore → terminal
localhost:1502 → sync Modbus worker → int16 × 0.1C mapping ───────┘
```

源码边界对应三层，但采用具体实现而非共同基类：

| 层 | 当前实现 | 不承担 |
|---|---|---|
| I/O（软件“物理”接入） | app 内 SocketCAN/epoll worker；同步 Modbus client worker | 不解析设备语义，不是物理 CAN/传感器证据 |
| 协议→数据 Adapter | `can_status_adapter`；`modbus_temperature_adapter` | 不拥有 fd，不创建线程 |
| 数据 | `observation` 的强类型 sample、来源状态、stale 与 mutex snapshot | 不发送命令，不改变 Runtime 状态 |

严格说 TCP/SocketCAN 是 Linux I/O/传输接入，不是电气物理层；当前对端仍是 `vcan` 与
localhost 模拟器。

## 为什么不是统一 `IBus`

CAN、Modbus 与 EtherCAT 的调度和失败语义不同：CAN 是帧事件；Modbus 是有 transaction
和 timeout 的串行事务；EtherCAT 是周期过程映像、WKC 与总线状态机。当前只共享“解码后
观测快照”，不共享 `read_all_signals()`，也不使用字符串 `get_int16("...")`。这样不会让
Modbus 网络超时阻塞 Runtime 的周期监督线程。

`ObservationStore` 是进程内 mutex 快照，不是 POSIX shared memory。当前所有消费者都在
一个演示进程内，为跨进程共享内存增加 ABI、生命周期和崩溃恢复合同没有收益。

## 构建与单测

需要 `experiments/modbus_tcp/` 相同的 `libmodbus-dev` 依赖：

```bash
cmake -S experiments/multibus_observer -B build/multibus_observer -DCMAKE_BUILD_TYPE=Debug
cmake --build build/multibus_observer -j
ctest --test-dir build/multibus_observer --output-on-failure
```

单测不打开 socket，覆盖 CAN 类型筛选、Holding Register 有符号/缩放映射、来源失败与
最后好样本保留、stale 判断。

## 三终端 Demo

先按仓库既有方式准备 `vcan0`，并构建 `linux/` 的节点模拟器：

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
```

终端 A：模拟 CAN 节点（当前合同只有数字输入/故障，不虚构关节角度）：

```bash
./build/linux/rcr_node_sim --can vcan0 --node-id 1 --duration-ms 10000
```

终端 B：把 Holding Register 0 预置为 `255`，设备合同解释为 `25.5C`：

```bash
./build/multibus_observer/mbus_sensor_server --temperature-deci-c 255
```

终端 C：同时显示两路观测：

```bash
./build/multibus_observer/rcr_multibus_observer \
  --can vcan0 --node-id 1 \
  --modbus-host 127.0.0.1 --modbus-port 1502 \
  --temperature-register 0 --duration-ms 5000
```

输出形如：

```text
CAN: node=1 input_bits=0x0000 interlock=ready fault=0 age_ms=4 stale=no source=healthy | Modbus: holding[0] temp=25.5C age_ms=3 stale=no source=healthy
```

停掉终端 B 后，Modbus source 会变为 `faulted`，最后好温度仍显示但年龄增长并最终
`stale=yes`；它不会把 CAN/Runtime 自动切到 HOLD。

## 证据边界

- 证明：同一 Linux 进程可隔离两种 I/O 时间模型，并发布带来源/时间/健康的类型化快照；
- 不证明：共享时间片分配、EtherCAT、PREEMPT_RT、物理 CAN、真实传感器或机器人闭环；
- `mbus_sensor_server` 和 `vcan` 都是模拟对端，不是现场设备证据；
- EtherCAT 出现前不为它添加占位 `IBus`、DC 或伺服字段。
- **不证明**：观测快照已接入 `rcrd` 命令路径。与执行段的接点仅为文档合同，见
  [`docs/OBSERVATION_TO_EXECUTION_CONTRACT.md`](../../docs/OBSERVATION_TO_EXECUTION_CONTRACT.md)
  （Deferred，未实现）。
