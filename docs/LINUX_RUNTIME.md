# Linux Runtime 模块原理

**Role**：本文是已实现 Linux Runtime 机制的学习与实现说明，不是系统 Architecture 或当前
Gate。先读 [ARCHITECTURE.md](ARCHITECTURE.md)，代码归属查
[CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md)，进程生命周期合同查
[RCRD_CONTRACT.md](RCRD_CONTRACT.md)。Workbench / Qt 不在这里，见
[workbench/README.md](workbench/README.md)。

## 1. 数据与线程关系

```text
Application 线程
  ├─ handle(event) ────────────────┐
  ├─ set_interlock_ready()         │ state mutex
  └─ publish_output_command() ─────┼─ CommandMailbox（latest-wins）
                                   │
PeriodicScheduler 周期线程         │
  ├─ clock_nanosleep(ABSTIME)      │
  ├─ watchdog / ACK timeout ───────┘
  └─ best-effort TraceBuffer

RuntimeDaemon 的 I/O 线程
  └─ EpollReactor(SocketCAN, eventfd, signalfd)
```

## 2. `PeriodicScheduler`

解决的问题：为 heartbeat、watchdog 和统计提供稳定监督周期，避免相对 sleep 的
累计漂移。

- 独占一个 `std::thread`，统一使用 `CLOCK_MONOTONIC`。
- 通过 `clock_nanosleep(TIMER_ABSTIME)` 等待绝对目标时间。
- callback 过载时跳到下一个未来边界，不追赶过期周期。
- FIFO 优先级为 1～99 时，线程内调用 `pthread_setschedparam`。
- `require_fifo=true` 时设置失败会拒绝启动；否则继续运行并保留 `fifo_error`。
- sleep、取时钟或 callback 异常会停止 worker 并保留 `worker_error`。

### Worker 失败合同（已冻结）

Core 对周期 worker 失败采取 fail-closed，但不自动升级应用状态：

1. worker 因 sleep/时钟/callback 异常退出后，`running()==false`，`stats().worker_error!=0`；
2. `publish_output_command` 与 `try_consume_output_command` 均拒绝/清空，mailbox 不得继续流向 I/O；
3. `RuntimeSnapshot.mode` 可能仍为 `Active`，`fault` 可能仍为 `None`——这是有意的职责边界，
   不是“已安全停机”的声明；
4. `RuntimeDaemon` 监督 `!running || worker_error!=0`，记录可见 Fault，并以非零退出码结束进程。

它只提供软件监督周期，不运行机器人算法，也不构成硬实时保证。

## 3. `EpollReactor`

解决的问题：集中等待多个 Linux fd，避免轮询和一设备一线程。

- 对象拥有 epoll fd，不拥有注册的 socket/pipe/eventfd。
- 调用者必须先 remove 再关闭业务 fd。
- `wait` 对 `EINTR` 重试，其他内核错误显式返回。
- 同一实例只允许一个等待线程；业务 callback 不在组件内部隐藏执行。

除 pipe 单测覆盖 ready、timeout 和非法 fd 外，`RuntimeDaemon` 已通过 `CanIoLoop` 将
SocketCAN、`eventfd` 和 `signalfd` 注册到 reactor。reactor 只负责等待与事件分派，业务 fd
仍由组合层按“请求停止、join worker、remove、close”的顺序关闭。

## 4. `RuntimeStateMachine`

解决的问题：把输出是否可接受变成显式状态，而不是散落的布尔判断。

- 初始和 stop 后为 `Disabled`；Boot 后到 `Idle`。
- 只有 `Active && interlock_ready` 才接受普通输出。
- timeout 或联锁丢失进入 `Hold`。
- Resume 只从 Hold 回到 Idle，必须重新 Activate。
- EStop 锁存并要求联锁闭合后显式 Reset；复位同样只到 Idle。
- 纯 `RuntimeStateMachine` 仍可分别测试 fault code 与 `FaultDetected` 规则；并发组合层只暴露
  `LinuxRuntime::raise_fault(code)`，在同一 `state_mutex_` 临界区完成故障码、迁移、
  watchdog/mailbox/session 清理和 trace，避免两阶段 TOCTOU 窗口。

这里的联锁和 EStop 是软件学习模型，不是硬件安全声明。

## 5. `CommandMailbox`

解决的问题：消费者只需要最新普通输出目标，不应追赶已经失效的旧目标。

- 单槽 latest-wins；新命令覆盖未读命令并增加 drop 计数。
- mutex 保证复合 `OutputCommand` 的一致快照。
- publish/consume/drop 原子计数只用于观测，不承担同步。
- 输入边沿、fault 和状态事件不能静默覆盖，后续应走单独的有界事件队列。

## 6. `MonotonicWatchdog`

解决的问题：Active 状态必须持续收到新鲜命令；超时后关闭 Linux 本地的后续命令传播。
已经 Applied 的节点普通输出不由这个 watchdog 直接拥有，而由 CAN endpoint lease 在命令
deadline 到期时归零。

- Runtime 进入 Active 时 arm，接受命令时 kick，周期线程 check。
- 时间戳属于 `CLOCK_MONOTONIC` 纳秒域，墙钟调整不影响超时。
- 首次超时返回 `newly_expired`，后续检查不重复触发迁移。
- 离开 Active 时 disarm；恢复不会自动消费旧命令。

## 7. `TraceBuffer`

解决的问题：保留调度、状态、命令拒绝和 watchdog 的内存证据，同时避免诊断阻塞
监督线程。

- 构造时一次性分配固定容量，满后覆盖最旧事件。
- 周期路径用 `try_lock`；读者占锁时丢 trace 并增加 dropped。
- snapshot 在非周期上下文复制出按时间顺序排列的数据。
- trace 丢失不得改变 Runtime 行为。

## 8. `LinuxRuntime`

解决的问题：提供唯一组合根，统一 Scheduler、StateMachine、Mailbox、Watchdog 和
Trace 的生命周期与并发边界。

进入 Active 和接受命令都要求 Scheduler 正在运行；否则 watchdog 无人检查。其余命令
接受条件是状态 Active、联锁就绪、session/sequence/mask
非零、sequence 递增、deadline 严格晚于当前单调时间。首次命令绑定活动 session；
Active 中切换 session 被拒绝。消费时再次检查状态和 deadline。

消费端也检查 Scheduler 是否仍在运行；如果周期 worker 异常退出，会清空 mailbox，
避免最后一条命令在失去监督后继续流向 I/O。Core 不会因此自动把状态机改成 Fault/Hold；
该可见性升级由 `RuntimeDaemon` 负责（见上文 Worker 失败合同）。

状态检查、session 更新、mailbox publish 和 watchdog kick 位于同一状态锁内，避免
刚进入 Hold 后仍有命令穿过。离开 Active 时清空 mailbox、disarm watchdog 并清除
session/sequence。

SocketCAN 成功写出后才登记 `last_sent_session/sequence/time`，此时 Runtime 只允许一笔
在途 ACK。后续发布仍可 latest-wins 覆盖 mailbox 中尚未发送的目标，但消费端在 ACK
闭合前不会取出下一条。只有 session、wire sequence 与 `APPLIED` 同时匹配才确认；过期、
stale、session mismatch 和乱序 ACK 更新 `last_ack_*`/`unexpected_ack_count`，但不解锁发送。
若正确 ACK 在 `output_ack_timeout` 内未到，周期线程设置 `FaultCode::AckTimeout`、进入 Hold、
清空输出路径且不自动重试；Resume 仍只回 Idle，必须重新 Activate。
这些字段证明软件执行确认闭环，不代表物理执行器动作或功能安全。

## 9. SocketCAN 与 CAN V1 codec

`SocketCan`、`FakeCanBus` 与 `rcr::can_v1` codec 已实现独立测试。codec 把四类 wire
消息与 `CanFrame` 显式转换（大端、固定 DLC=8），不拥有 fd 或节点状态；
`SocketCan::native_handle()` 由 `CanIoLoop` 注册到 `EpollReactor`。库内只读探测 CAN 接口，
创建入口仍是 `linux/scripts/setup_vcan.sh`。

独立节点模拟器 `rcr_node_sim` 与双进程验收 `rcr_vcan_acceptance` 已实现。缺 `vcan0`
时验收失败。完整跑法：

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
./linux/scripts/run_vcan_acceptance.sh vcan0
```

## 10. Benchmark

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j

./build/linux/rcr_benchmark --duration-ms 60000 --period-us 1000
./build/linux/rcr_benchmark --duration-ms 60000 --period-us 1000 \
  --fifo-priority 80 --require-fifo
```

输出的 lateness 是实际唤醒时间减绝对目标时间。每份正式证据还要记录设备、内核、
governor、权限、负载和时长。空 callback 只代表调度唤醒基线，不代表 CAN 端到端
时延、机器人算法周期或硬实时能力。

## 11. 当前验证

本地 CMake 构建产生 **24** 个默认 CTest 目标（含 Workbench）。`test_can_v1` /
`test_node_sim` 分别验证编解码与节点业务逻辑。`test_socketcan_vcan`、daemon/rcrd
进程测试和 Workbench vcan health 在缺少 `vcan0` 或无权打开 `PF_CAN` 时由 CTest
记为 Skipped。

双进程验收不进默认 CTest（缺接口应失败，不能拖红日常单测）：

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
./linux/scripts/run_vcan_acceptance.sh vcan0
```
