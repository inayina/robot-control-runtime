# 全项目模块知识卡

状态：Living document  
适用范围：当前工作树中已经存在的 Linux Runtime、CAN、模拟器、daemon、应用与证据工具。

本文是 [`KNOWLEDGE_BASE.md`](KNOWLEDGE_BASE.md) 的模块化学习入口。每个模块只维护一张卡；
新增或实质修改模块时，必须在同一变更中更新对应卡。卡片只写能由源码、测试或实机证据确认
的内容，不把规划写成现状。`我还没理解的地方` 由学习者阅读代码并完成观察实验后填写。

## 1. 基础类型与显式错误

模块：`types.hpp` / `result.hpp`（公共类型与 `Result<T>`）  
一句话作用：定义 Runtime 状态、事件、故障、进程内命令、CAN 帧和统一的显式错误返回。

上游调用者：几乎全部 Linux 模块。  
下游依赖：固定宽度整数、`std::string`、`std::string_view`。

输入：状态值、命令字段或错误信息。  
输出：强类型对象、可读名称或 `Result<T>`。

运行线程：不创建线程，在调用者线程执行。  
使用时钟：`OutputCommand::deadline_ns` 约定属于 `CLOCK_MONOTONIC`，本模块不读取时钟。

拥有的资源：`Error` 可能拥有诊断字符串；其他类型主要是值对象。  
资源关闭顺序：普通 C++ 对象自动析构。

正常路径：纯用户态值传递；调用者先检查 `ok()` / `operator bool`，再读取 `value()`。验证：`ctest --test-dir build/linux -R 'test_(runtime|can_v1)'`。  
失败路径：返回 `Errc` 和诊断文本，不让异常跨越控制边界；当前 `Result<T>` 仍要求 `T` 可默认构造。

为什么不用另一种方案：不直接把 errno、异常或 CAN wire struct 当公共业务类型，避免错误分类和
进程内存布局侵入控制与协议边界。

我还没理解的地方：（学习者填写）

## 2. 单调时间工具

模块：`time.hpp` / `time.cpp`  
一句话作用：统一读取本机单调时间，并在纳秒与 `timespec` 之间换算；另提供仅用于证据标注的墙钟采样。

上游调用者：Scheduler、Runtime、CAN I/O、节点模拟器、Workbench TestRunner。
下游依赖：POSIX `clock_gettime` 和 `timespec`。

输入：无，或纳秒/`timespec`。  
输出：单调纳秒、墙钟 Unix 纪元纳秒或规范化的时间结构。

运行线程：在调用者线程执行。  
使用时钟：控制路径只用 `CLOCK_MONOTONIC`；`realtime_now_ns()` 读 `CLOCK_REALTIME`。

拥有的资源：无。  
资源关闭顺序：无。

正常路径：用户态调用 POSIX API，内核提供时钟；单调结果用于 deadline 和 watchdog。验证：`ctest --test-dir build/linux -R test_scheduler`。
失败路径：`clock_gettime` 失败返回 `IoError`。单调时钟失败时不退化到墙钟。

为什么不用另一种方案：控制时间不用 `CLOCK_REALTIME`，因为系统校时会破坏超时和截止时间语义；结果文件需要墙钟时单独采样，避免两个时间域混用。

我还没理解的地方：（学习者填写）

## 3. 周期调度器

模块：`PeriodicScheduler`  
一句话作用：按绝对单调时间边界运行周期回调，并记录唤醒延迟、miss 和真实调度属性。

上游调用者：`LinuxRuntime`、`rcr_benchmark`。  
下游依赖：时间工具、`std::thread`、pthread affinity、`SCHED_FIFO`、`clock_nanosleep`。

输入：周期、FIFO 优先级、CPU affinity 和回调。  
输出：`SchedulerTick` 与 `SchedulerStats`。

运行线程：创建一个周期 worker。  
使用时钟：`CLOCK_MONOTONIC` + `TIMER_ABSTIME`。

拥有的资源：一个 `std::thread`、启动握手 mutex/condition variable 和原子统计。  
资源关闭顺序：`request_stop` → 等待当前周期边界 → `join`。

正常路径：用户态提交期限，内核负责睡眠、唤醒和线程调度；过载时按跨过的边界累计
`deadline_misses` 并跳到未来绝对边界。验证：`ctest --test-dir build/linux -R test_scheduler`
（含 `OverloadSkipsMissedDeadlinesWithoutCatchUp`）。  
失败路径：配置非法不启动；强制 FIFO/affinity 失败使启动失败；时钟或回调异常结束 worker 并记错。

为什么不用另一种方案：不用相对 sleep，避免执行时间积累成长期漂移；不用追赶式补跑，避免过载风暴。
`wakeup_lateness` 只测唤醒相对计划边界，不把 callback 执行时间算进 lateness。

我还没理解的地方：（学习者填写）

## 4. Runtime 状态机

模块：`RuntimeStateMachine`  
一句话作用：实现 Disabled、Idle、Active、Hold、Fault、EStop 的确定性软件迁移规则。

上游调用者：`LinuxRuntime`。  
下游依赖：公共状态、事件和故障类型。

输入：`RuntimeEvent`、软件联锁和故障码。  
输出：接受或拒绝的 `TransitionResult`。

运行线程：不创建线程；通常在 Runtime 状态锁内运行。  
使用时钟：无。

拥有的资源：模式、联锁和故障三个进程内状态。  
资源关闭顺序：Runtime stop 时整体重建为默认状态。

正常路径：规则完全在用户态执行；Boot 到 Idle，联锁满足后 Activate，恢复只回 Idle。验证：`ctest --test-dir build/linux -R test_state_machine`。  
失败路径：非法迁移保持原状态并返回原因；EStop 锁存后只能走显式 Reset 路径。
Hold（Watchdog/AckTimeout/InterlockLost）与 Fault（raise_fault）的分工见
[故障分类数据流](images/fault-classification-flow.svg)。

为什么不用另一种方案：内部不加锁，由组合层统一串行化，避免隐藏锁顺序和两套并发语义。

我还没理解的地方：（学习者填写）

## 5. 命令 Watchdog

模块：`MonotonicWatchdog`  
一句话作用：检测 Active 状态下是否长时间没有收到合法新命令。

上游调用者：`LinuxRuntime`。  
下游依赖：原子变量和 `std::chrono`。

输入：arm、kick、check 时的单调纳秒。  
输出：Disarmed、Healthy、Expired 及首次过期标记。

运行线程：Application 发布命令，周期线程检查；复合语义由 Runtime 状态锁串行化。  
使用时钟：调用者提供的 `CLOCK_MONOTONIC` 时间。

拥有的资源：最后 kick 时间和 armed/expired 原子锁存。  
资源关闭顺序：离开 Active 时先 disarm，再清命令路径。

正常路径：用户态比较单调时间戳；合法命令刷新基准，未到 timeout 返回 Healthy。验证：`ctest --test-dir build/linux -R test_watchdog`。  
失败路径：首次超时返回 `newly_expired=true`，Runtime 随后进入 Hold 并清旧输出。

为什么不用另一种方案：watchdog 不创建自己的线程或直接操作状态机，避免第二套调度和状态所有权。

我还没理解的地方：（学习者填写）

## 6. Latest-wins 命令邮箱

模块：`CommandMailbox`  
一句话作用：在线程之间传递最新一条普通输出目标。

上游调用者：`LinuxRuntime`。  
下游依赖：mutex、optional、atomic。

输入：经过 Runtime 校验的 `OutputCommand`。  
输出：最新命令或空结果，以及发布、消费和覆盖计数。

运行线程：Application 生产，CAN I/O 消费。  
使用时钟：本身不读取时钟。

拥有的资源：一个可选命令槽和互斥锁。  
资源关闭顺序：离开 Active 时清槽；对象析构自动释放同步资源。

正常路径：纯用户态同步；发布保存最新值，消费者在同一锁区间复制并清空。验证：`ctest --test-dir build/linux -R test_mailbox`。  
失败路径：存在未读值时被新命令覆盖并增加 drop 计数；这不是输入事件丢失语义。

为什么不用另一种方案：普通目标适合 latest-wins，追赶旧目标没有意义；故障和输入边沿另走有界队列。

我还没理解的地方：（学习者填写）

## 7. Trace 环形缓冲区

模块：`TraceBuffer`  
一句话作用：以固定容量保存近期调度、迁移、命令和 watchdog 诊断事件。

上游调用者：`LinuxRuntime`。  
下游依赖：预分配 vector、mutex、atomic。

输入：`TraceEvent`。  
输出：按逻辑时间顺序复制出的诊断快照。

运行线程：周期线程和 Application 可写；非周期诊断路径读取。  
使用时钟：事件通常携带 `CLOCK_MONOTONIC` 纳秒。

拥有的资源：固定容量环形数组和互斥锁。  
资源关闭顺序：无需显式关闭。

正常路径：纯用户态写入下一槽，写满后覆盖最旧事件。验证：`ctest --test-dir build/linux -R test_trace`。  
失败路径：周期写入拿不到锁时丢 trace 并计数，不阻塞监督路径。

为什么不用另一种方案：不用周期内写磁盘或无界 vector，避免 I/O、扩容和慢读者放大周期延迟。

我还没理解的地方：（学习者填写）

## 8. Lateness 统计

模块：`stats.hpp` / `stats.cpp`  
一句话作用：在非周期上下文计算 lateness 的均值和 P50/P95/P99/P99.9。

上游调用者：`rcr_benchmark`。  
下游依赖：C++20 `std::span`、vector、排序和浮点运算。

输入：一组纳秒样本。  
输出：单个分位数或 `PercentileSummary`。

运行线程：非周期调用线程。  
使用时钟：不读时钟，只处理已有样本。

拥有的资源：排序用的样本副本。  
资源关闭顺序：函数返回后自动释放。

正常路径：纯用户态复制、排序并使用固定的线性插值算法。验证：`ctest --test-dir build/linux -R test_stats`。  
失败路径：空样本、越界百分位或非有限输入返回 `InvalidArgument`。

为什么不用另一种方案：暂不在周期内维护直方图；原始样本更容易复算和审查，且不侵入周期路径。

我还没理解的地方：（学习者填写）

## 9. 有界输入队列

模块：`BoundedInputQueue`  
实现位置：`linux/src/core/bounded_input_queue.cpp`；无设备恢复策略。
一句话作用：可靠地把 CAN 输入、故障和边沿从 I/O 线程交给周期监督线程。

上游调用者：`CanIoLoop`。  
下游依赖：固定容量 vector、mutex、atomic。

输入：按值保存的 `RuntimeInputEvent`。  
输出：按到达顺序弹出的事件、push/drop/overflow 计数和 overflow 锁存；DaemonSnapshot 只读聚合这些统计。

运行线程：I/O 单生产者，周期线程单消费者。  
使用时钟：事件携带 I/O 接收时采样的单调时间，本队列不读时钟。

拥有的资源：构造时固定分配的环形队列。  
资源关闭顺序：Daemon 停止生产和消费线程后销毁。

正常路径：纯用户态队列同步；事件依次入队，监督线程按预算消费。验证：`ctest --test-dir build/linux -R test_runtime_events`。  
失败路径：满队列拒绝新事件、计数并锁存 overflow，随后升级为内部故障。

为什么不用另一种方案：不用 latest-value atomic，因为重启和故障边沿不能被后值静默覆盖；当前规模也不需无锁队列。

我还没理解的地方：（学习者填写）

## 10. 单节点监督器

模块：`NodeSupervisor`  
实现位置：`linux/src/supervision/node_supervisor.cpp`；属于 Device Supervision，不是纯 Core。
一句话作用：监督一个 CAN 节点的在线状态、会话、重启、节点故障、心跳、数字输入快照和 OutputStatus。

上游调用者：`LinuxRuntime` 的周期 supervision hook。  
下游依赖：`BoundedInputQueue` 和 `LinuxRuntime` 状态 API。

输入：Heartbeat、NodeStatus、OutputStatus、ProtocolReject、IoError。  
输出：节点快照（含最后一次 `input_bits` / `last_output_mirror`）及对 Runtime 的联锁、原子
Fault、CommLoss 与 ACK 观察调用；clear 前汇总检查全部持久 blocker，不把最后一个
FaultCode 当 active fault set。`input_bits` bit0 是 POSITION_REACHED 观测，监督器只保留
它，不把它升级为 Fault；`fault_code != 0` 仍走 NodeFault。

运行线程：周期调度线程。  
使用时钟：接收端 `CLOCK_MONOTONIC`。

拥有的资源：单节点历史状态；只借用输入队列。  
资源关闭顺序：先停止 I/O 和周期线程，再由 Daemon 销毁 supervisor 与队列。

正常路径：用户态消费已解码事件并驱动 Runtime；首次心跳建立会话，每周期检查通信年龄；
OutputStatus 按值交回 Runtime 匹配在途命令，同时保留 `output_mirror` 只读副本。验证：
`ctest --test-dir build/linux -R test_runtime_events`。
失败路径：心跳超时、节点重启、节点故障或队列溢出使 Runtime 离开 Active；overflow 要求
重启 daemon，后来的故障分类不能覆盖并绕过它。到位光电（bit0=1）单独出现时 Runtime 保持
当前模式。总览：
[故障分类数据流](images/fault-classification-flow.svg)。

为什么不用另一种方案：V1 只有一个真实节点监督需求，不提前扩展为通用多节点消息总线。

我还没理解的地方：（学习者填写）

## 11. Linux Runtime 组合根

模块：`LinuxRuntime`  
实现位置：`linux/src/runtime/linux_runtime.cpp`；属于 Runtime Semantics，不是 daemon 生命周期。
一句话作用：组合 Scheduler、状态机、watchdog、mailbox、单笔 ACK 状态、trace 和监督钩子，形成 Runtime Core。

上游调用者：`RuntimeDaemon`、测试和未来 Application Adapter。  
下游依赖：全部 Runtime Core 子模块。

输入：生命周期事件、软件联锁、故障、输出命令、发送成功事实和 OutputStatus。
输出：状态迁移、待发送命令、ACK 门控、trace 和 Runtime 快照。

运行线程：Application/I/O 调用公开 API；唯一周期线程执行 `on_tick`。  
使用时钟：`CLOCK_MONOTONIC`。

拥有的资源：Scheduler、状态机、watchdog、mailbox、唯一在途 ACK、trace 和活动会话历史。
资源关闭顺序：停止并 join scheduler → 取得状态锁 → disarm watchdog → 清 mailbox/session → 复位状态机。

正常路径：用户态组合规则，Scheduler 再请求内核调度；显式 Boot/Activate 后监督命令。验证：`ctest --test-dir build/linux -R test_runtime`。  
失败路径：无 scheduler、非 Active、旧 session/sequence、过期 deadline 均拒绝；故障通过
`raise_fault` 单锁关闭；ACK 不匹配计数，超时分类为 `AckTimeout` 并进入 Hold；worker 消失后两端 fail closed。

为什么不用另一种方案：不直接拥有 SocketCAN fd，避免控制状态、时间监督和 Linux I/O 生命周期耦合；
不建重试队列，因为 V1 尚未冻结多笔在途、幂等重发与过期优先级合同。

我还没理解的地方：（学习者填写）

## 12. fd RAII、停止和信号

模块：`OwnedFd` / `EventFd` / `SignalFd`  
一句话作用：表达 fd 唯一所有权，并把跨线程停止和 SIGINT/SIGTERM 转成可读 fd。

上游调用者：`RuntimeDaemon`、`CanIoLoop`。  
下游依赖：Linux `close`、`eventfd`、`signalfd`、pthread signal mask。

输入：已有 fd、停止请求或进程信号。  
输出：可注册到 epoll 的句柄和 drain 结果。

运行线程：main 创建；I/O 线程读取；signal mask 必须由创建线程恢复。  
使用时钟：无。

拥有的资源：OwnedFd 独占 fd；SignalFd 还保存原 signal mask。  
资源关闭顺序：停止 I/O 并 join → 关闭 signalfd/eventfd → 在创建线程恢复 signal mask。

正常路径：用户态 RAII 管 owner，内核维护 fd 计数和信号队列；eventfd/signalfd 进入统一等待。验证：`ctest --test-dir build/linux -R test_owned_fd`。  
失败路径：创建、读写或恢复失败返回 Result；拒绝在错误线程恢复 signal mask。

为什么不用另一种方案：不用裸 `int` 或共享所有权；condition variable 也不能直接唤醒 `epoll_wait`。

我还没理解的地方：（学习者填写）

## 13. epoll Reactor

模块：`EpollReactor`  
一句话作用：封装 Linux epoll 的注册、修改、删除和等待操作。

上游调用者：`CanIoLoop`、`rcr_node_sim`。  
下游依赖：`epoll_create1`、`epoll_ctl`、`epoll_wait`。

输入：非 owning fd、关注事件、超时和单次最大事件数。  
输出：ready fd 与 EPOLLIN/ERR/HUP 位集合。

运行线程：通常由唯一 I/O 线程调用 `wait`。  
使用时钟：epoll 相对毫秒 timeout；不负责业务 deadline。

拥有的资源：epoll 实例 fd；不拥有被监听的业务 fd。  
资源关闭顺序：先 DEL 业务 fd → 业务 owner close → reactor 析构关闭 epoll fd。

正常路径：用户态注册 interest list，内核维护等待队列并返回 readiness。验证：`ctest --test-dir build/linux -R test_epoll_reactor`。  
失败路径：非法参数或 epoll syscall 错误通过 Result 返回。

为什么不用另一种方案：不做 callback framework，也不存对象指针，避免悬空生命周期和隐藏控制流。

我还没理解的地方：（学习者填写）

## 14. SocketCAN 与内存替身

模块：`SocketCan` / `FakeCanBus`  
一句话作用：提供真实 Linux SocketCAN 收发后端和确定性的内存单测替身。

上游调用者：`CanIoLoop`、模拟器、验收程序和单元测试。  
下游依赖：PF_CAN、CAN_RAW、ioctl、bind、select、read/write。

输入：接口名、`CanFrame` 和接收超时。  
输出：接收帧或 Timeout、WouldBlock、IoError。

运行线程：SocketCan 通常由 I/O 线程独占；Fake 在测试线程运行。  
使用时钟：有限 receive 使用 `select` 相对 timeout。

拥有的资源：SocketCan 独占 CAN socket fd；Fake 拥有内存队列。  
资源关闭顺序：上层先从 epoll 删除，再 close；析构兜底。

正常路径：用户态搬运 `CanFrame`，内核 SocketCAN 绑定 netdevice 并收发 `can_frame`。验证：`ctest --test-dir build/linux -R 'test_(fake_can|socketcan)$'`。  
失败路径：未打开、接口错误、超长帧、WouldBlock、短读写或 syscall 错误均返回 Result。

为什么不用另一种方案：接口只服务当前 CAN 的真实/测试实现，不把 Modbus/EtherCAT 强塞进通用 Transport。

我还没理解的地方：（学习者填写）

## 15. CAN/vcan 接口边界

模块：`vcan.cpp` / `setup_vcan.sh`  
一句话作用：Runtime 库只读确认接口是否为 CAN，运维脚本才负责创建和启用 vcan。

上游调用者：Daemon、模拟器、验收程序和开发者。  
下游依赖：sysfs；脚本依赖 `ip`、`modprobe` 和 CAP_NET_ADMIN/root。

输入：接口名。  
输出：Available、Missing、NotCan、InvalidName，或配置后的 vcan 接口。

运行线程：应用启动线程或运维 shell。  
使用时钟：无。

拥有的资源：库不持有 fd；vcan netdevice 由系统管理员管理。  
资源关闭顺序：探测无需关闭；接口删除不属于 Runtime 退出路径。

正常路径：库读取内核 sysfs 快照；运维脚本经 netlink 工具配置 vcan。验证：`ctest --test-dir build/linux -R test_vcan`。  
失败路径：名称非法、接口缺失/类型错误或权限不足明确失败。

为什么不用另一种方案：库不调用 shell、不隐式改宿主网络，避免隐藏 root 权限和不可审计副作用。

我还没理解的地方：（学习者填写）

## 16. CAN V1 合同与 Codec

模块：`can_v1.hpp` / `can_v1.cpp` / `protocol/can_v1`  
一句话作用：在语义 DTO 和冻结的 8 字节 CAN V1 报文之间进行无状态编解码。

上游调用者：`CanIoLoop`、`CanNodeLogic`、模拟器和验收程序。  
下游依赖：CAN V1 线级合同和 golden vectors。

输入：四类 wire DTO 或原始 `CanFrame`。  
输出：合法帧、解码消息或明确拒绝。

运行线程：在调用者线程执行。  
使用时钟：不读时钟；只换算相对有效期与本地 deadline。

拥有的资源：无 fd、线程或历史状态。  
资源关闭顺序：无。

正常路径：codec 纯用户态逐字段处理，内核只在后续 socket write/read 看见帧。验证：`ctest --test-dir build/linux -R test_can_v1`。  
失败路径：本地 DTO 违法返回 InvalidArgument；外部非法帧返回 Rejected。

`NodeStatus.input_bits` 的线级宽度仍是任意 u16；闭环演示把 bit0 文档冻结为
`POSITION_REACHED`（`kInputBitPositionReached`），不升 `protocol_version`，不改 golden
vector 字节。CellReady / Modbus DO0 / LIGHT_ON 不是 CAN 字段。

为什么不用另一种方案：不用结构体 memcpy，避免 padding、对齐和主机字节序；四类消息无需 ISO-TP。

我还没理解的地方：（学习者填写）

## 17. CAN I/O 线程

模块：`CanIoLoop`  
一句话作用：在单一 I/O 线程中完成 SocketCAN 收发、协议转换、事件入队和停止处理。

上游调用者：`RuntimeDaemon`。  
下游依赖：SocketCan、EpollReactor、eventfd、signalfd、codec、Runtime、输入队列。

输入：CAN readiness、停止/信号 fd 和 Runtime 输出 mailbox。  
输出：输入事件、CAN OutputCommand、I/O 统计和停止原因。

运行线程：创建一个 I/O worker。  
使用时钟：接收采样和输出 deadline 使用 `CLOCK_MONOTONIC`；epoll 使用 10 ms timeout 推进输出泵。

拥有的资源：SocketCan、epoll 和 I/O thread；只借用 daemon 的 eventfd/signalfd。  
资源关闭顺序：写停止 eventfd → join I/O → epoll DEL → close socket；借用 fd 由 Daemon 稍后关闭。

正常路径：用户态解释 readiness 和协议，内核负责 epoll/SocketCAN/fd 唤醒。验证：`ctest --test-dir build/linux -R test_runtime_daemon`。  
失败路径：解码拒绝计数；队列满锁存；读写/EPOLLERR/HUP 结束 worker；WouldBlock 保留 pending 重试。

为什么不用另一种方案：一个 epoll 线程已覆盖 V1 的单 CAN fd 和退出事件，无需阻塞接收加信号线程。

我还没理解的地方：（学习者填写）

## 18. 模拟节点业务逻辑

模块：`CanNodeLogic`  
一句话作用：实现模拟节点的 session、序号、有效期、普通输出 lease、联锁和输出应用规则。

上游调用者：`rcr_node_sim` 和单元测试。  
下游依赖：CAN V1 codec 和 DTO。

输入：CAN 帧或解码命令，以及接收/应用/lease 轮询单调时间。

输出：OutputStatus、带有界 lease 的输出镜像、heartbeat、status 或协议拒绝计数。

运行线程：不创建线程；模拟器单线程串行调用。  
使用时钟：调用者提供 `CLOCK_MONOTONIC` 纳秒。

拥有的资源：节点 boot/session、序号历史、输出状态与 lease deadline；不拥有时钟/timerfd。

资源关闭顺序：无系统资源；lease 到期或联锁丢失清普通输出，soft restart 再清会话历史。

正常路径：纯用户态按 session、联锁、序号、有效期顺序判定；Applied 使用同一 deadline
建立 lease。验证：`ctest --test-dir build/linux -R test_node_sim`。

失败路径：返回 SessionMismatch、NotReady、StaleSequence 或 Expired 且不刷新 lease；到期、
联锁丢失或重启归零；无法解码的命令只计数。

为什么不用另一种方案：不拥有 fd、timer 或线程，使业务规则能在无 vcan、无睡眠条件下确定性测试。

我还没理解的地方：（学习者填写）

## 19. 独立节点模拟器进程

模块：`rcr_node_sim`  
一句话作用：在 vcan 上以独立进程模拟一个 CAN 节点及默认关闭的故障场景。

上游调用者：用户、vcan 验收和故障矩阵。  
下游依赖：CanNodeLogic、SocketCan、epoll、timerfd、signalfd。

输入：CLI 参数、CAN 命令、SIGINT/SIGTERM、输出 lease 与故障注入 deadline。

输出：Heartbeat、NodeStatus、OutputStatus、非法测试帧和退出统计。

运行线程：单线程事件循环。  
使用时钟：timerfd、命令有效期和输出 lease 使用 `CLOCK_MONOTONIC`。

拥有的资源：CAN socket、epoll、signalfd、多个 timerfd 和延迟命令容器。  
资源关闭顺序：epoll DEL 全部 fd → close CAN → close timerfd/signalfd → 析构 epoll。

正常路径：用户态事件循环驱动逻辑；一个 one-shot timerfd 对准最早的延迟命令或 lease
deadline，内核提供 vcan/epoll/timerfd/signalfd。验证：`./linux/scripts/run_vcan_acceptance.sh vcan0`。

失败路径：接口/fd 创建失败退出；可注入停心跳、延迟、软重启和非法帧。

为什么不用另一种方案：独立进程和真实 vcan 路径能验证进程隔离；同进程 fake 不能提供该证据。

我还没理解的地方：模拟器仍保留应用内小型 `OwnedFd`，尚未完全复用公共 fd 封装。

## 20. Daemon 组合与生命周期

模块：`RuntimeDaemon`  
一句话作用：组装 Runtime、节点监督、输入队列、I/O 和完整进程生命周期。

上游调用者：`rcrd` main 和服务级测试。  
下游依赖：Runtime、NodeSupervisor、CanIoLoop、EventFd、SignalFd、CAN 接口探测。

输入：`DaemonConfig`、生命周期请求和测试/Application 命令。  
输出：聚合 Runtime/Node/I/O/输入队列统计的 `DaemonSnapshot`、经过全 blocker gate 的恢复结果、
稳定退出码和 reset 前的一次性 final summary。

运行线程：main、周期 worker、I/O worker；可选 duration worker。  
使用时钟：Runtime 用 `CLOCK_MONOTONIC`；duration 用 `steady_clock`。

拥有的资源：停止/信号 fd、队列、Runtime、Supervisor、I/O 和 duration thread。  
资源关闭顺序：停止并 join I/O → join duration → stop Runtime → 逆序销毁对象 → 关闭并恢复信号资源。

正常路径：用户态按顺序组装 owner，内核提供线程调度和 fd 机制。验证：
`ctest --test-dir build/linux -R 'DaemonRepeatStartStopFdAndThreadStable|test_runtime_daemon'`
（100 次 start/stop 后 `/proc/self/fd` 与 `Threads:` 必须回到基线）。

停止时先 join I/O，使队列不再有生产者，再在非周期上下文读取最终组合 snapshot；该视图用于
诊断，不是 persistent fault history，也不参与状态迁移。

独立进程重复测试必须先观察 `msg=daemon started` readiness 再采子进程 `/proc/<pid>/fd`；
只等 `fd >= 5` 会把启动中间态误判成稳定态。

接口 down（显式授权）：`sudo ./linux/scripts/run_vcan_iface_down_fault.sh vcan0`，期望
`WorkerFailure` 且 `stop_reason` 为 `IO_ERROR`/`SEND_FAILURE`。  
失败路径：配置、接口、权限和 worker 故障分别映射退出码；部分启动按逆序回滚；Fault clear
先拒绝仍 active 的 overflow/CommLoss/offline/node fault，再交给状态机迁移。

为什么不用另一种方案：不把生命周期全写进 main，才能可靠测试部分启动回滚和重复 stop；
仅靠子进程退出不能证明同进程 fd 回收。链路 down 必须显式授权，避免默认 CTest 改主机网络。

我还没理解的地方：（学习者填写）

## 21. rcrd 命令行入口

模块：`rcrd`  
一句话作用：解析最小启动参数，运行 RuntimeDaemon，并返回稳定进程退出码。

上游调用者：用户、未来 systemd、进程级测试。  
下游依赖：`RuntimeDaemon`。

输入：CAN 接口、节点、周期、超时、FIFO、affinity、duration。  
输出：日志和 0～4 退出码。

运行线程：main；内部 Daemon 创建 worker。  
使用时钟：入口不直接读时钟。

拥有的资源：栈上的 RuntimeDaemon。  
资源关闭顺序：`wait_and_stop` 完成关闭，析构提供幂等兜底。

正常路径：main 解析参数后交给 Daemon，退出信号由内核送入 signalfd。验证：
`ctest --test-dir build/linux -R RcrdRepeatStartStopFdStable`（子进程运行中 fd 数相对稳定，
父进程 fd/线程不随 fork 循环增长）。systemd 托管见下一张卡。  
失败路径：参数非法返回 ConfigError；启动/worker 错误返回分类退出码。

为什么不用另一种方案：当前配置少，不引入 YAML、REST、Unix socket 或测试控制入口。

我还没理解的地方：（学习者填写）

## 22. systemd 部署单元（P3-A1）

模块：`deploy/systemd/*.service`  
一句话作用：用 systemd 托管 vcan oneshot 与前台 `rcrd`，日志进 journal，崩溃限次重启。

上游调用者：板上/本机运维、`systemctl`。  
下游依赖：已安装的 `/opt/robot-control-runtime/current/bin/{setup_vcan.sh,rcrd,rcr_node_sim}`、
系统用户 `rcr`。

输入：unit 文件与可选 FIFO drop-in。  
输出：服务状态、journal 日志、退出码驱动的重启行为。

运行线程：由 systemd 拉起的 `rcrd` 进程（内部仍是 main + 周期 + I/O）。  
使用时钟：与 Runtime 相同（`CLOCK_MONOTONIC`）；systemd 用墙钟做 RestartSec/Timeout。

拥有的资源：unit 不拥有 Runtime 内 fd；只管理进程生命周期。  
资源关闭顺序：`systemctl stop` → SIGTERM → `TimeoutStopSec=5s` → 必要时 SIGKILL。

正常路径：`rcr-vcan` 先保证 `vcan0`，再启动 `User=rcr` 的 `rcrd`。静态验证：
`./deploy/systemd/verify_units.sh`。本机自测：`systemctl enable --now rcr-vcan rcrd`。  
失败路径：缺二进制/用户/接口 → 非零退出；30s 内最多自动重启 3 次；`--require-fifo`
缺 `LimitRTPRIO` 时应失败可见。

为什么不用另一种方案：不用 root 常驻 `rcrd`；不用 `WatchdogSec`（无 `sd_notify`）；
模拟器默认 disabled，避免生产路径绑死验收节点。

我还没理解的地方：ThinkPad enable ≠ Orange Pi 冷启动绿灯（B4）；板上 unit 已 enable 但
无 CAN 时 `rcrd` failed，不能写成生命周期关闭。

## 23. 周期唤醒 Benchmark

模块：`rcr_benchmark`  
一句话作用：测量周期线程唤醒 lateness；可选受控 callback 延迟验证过载 miss/跳周期。

上游调用者：用户和 ThinkPad/Orange Pi benchmark 矩阵 wrapper。  
下游依赖：PeriodicScheduler 和统计模块。

输入：duration、period、可选 `--callback-delay-us`（默认 0）、FIFO、affinity 和样本路径。  
输出：`callback_delay_us`、cycles、deadline_misses、lateness min/mean/max 与
P50/P95/P99/P99.9。

运行线程：main 加一个 scheduler worker。  
使用时钟：Scheduler 使用 `CLOCK_MONOTONIC`。

拥有的资源：预分配样本数组、Scheduler、可选输出文件。  
资源关闭顺序：stop → join → 非周期统计 → 写文件。

正常路径：callback 先写 lateness 样本，再按需 sleep；内核负责周期唤醒，join 后用户态统计。
空载验证：`./build/linux/rcr_benchmark --duration-ms 1000 --period-us 1000`。  
过载验证：`./build/linux/rcr_benchmark --duration-ms 200 --period-us 1000 --callback-delay-us 3000`。  
失败路径：权限、配置、worker、样本溢出或文件错误均明确记录或返回非零。

为什么不用另一种方案：不在周期内排序、分配或写盘；空回调隔离调度唤醒；过载延迟放在
benchmark 而非 Scheduler 配置，避免生产路径携带实验开关。delay ≠ lateness。

我还没理解的地方：该结果不是 CAN 延迟、控制响应时间或硬实时证明。2026-08-05 Orange Pi
5 秒矩阵已由 RT0 标为 pilot，见 `docs/REALTIME_EVIDENCE_SCHEMA.md`。

## 24. 双进程 vcan 验收

模块：`rcr_vcan_acceptance` / `run_vcan_acceptance.sh`  
一句话作用：验证验收进程与独立节点模拟器只通过 vcan 完成 CAN V1 端到端场景。

上游调用者：开发者。  
下游依赖：SocketCan、codec、`fork/exec/waitpid`、节点模拟器。

输入：接口、节点、模拟器路径和证据路径。  
输出：七场景 PASS/FAIL 和环境元数据。

运行线程：验收主进程加模拟器子进程。  
使用时钟：`steady_clock` 控制场景预算；协议使用相对有效期。

拥有的资源：CAN socket、子进程和证据文件。  
资源关闭顺序：SIGTERM 等待子进程，超时才 SIGKILL/waitpid → close socket → 关闭证据文件。

正常路径：两个用户进程只经内核 vcan/SocketCAN 通信并验证七场景（含普通输出 lease
到期归零）。验证：`./linux/scripts/run_vcan_acceptance.sh vcan0`。

失败路径：缺 vcan、模拟器不可执行、场景超时或断言失败都硬失败。

为什么不用另一种方案：FakeCanBus 不能证明 SocketCAN 内核路径和进程隔离。

我还没理解的地方：（学习者填写）

## 25. 自动故障矩阵

模块：`rcr_fault_matrix` / `run_fault_matrix.sh`  
一句话作用：把状态、命令、队列、worker、权限、通信和退出故障变成可重复场景。

上游调用者：开发者和后续 CI。  
下游依赖：Runtime、Daemon、模拟器、rcrd、vcan 和子进程管理。

输入：vcan、模拟器/rcrd 路径和新证据文件。  
输出：当前程序 **22** 个场景的 pass、failed、permission_denied、unsupported 或
not_run。已入库 clean 摘要仍是当时的 19/19，不能写成本树已关 22/22。

运行线程：主测试进程；部分场景启动子进程。  
使用时钟：`steady_clock` 控制等待预算，Runtime 使用单调时钟。

拥有的资源：测试对象、子进程和证据文件。  
资源关闭顺序：每场景回收子进程和 Runtime，最后关闭证据文件。

正常路径：用户态逐项驱动 Runtime/子进程并记录环境和分类结果。验证：`./linux/scripts/run_fault_matrix.sh vcan0`。  
失败路径：拒绝覆盖证据；缺 vcan 硬失败；failed/not_run 令程序非零退出。

为什么不用另一种方案：不用人工逐条操作，避免不可复现和把权限不足误报为代码通过。

我还没理解的地方：（学习者填写）

## 26. 构建、测试与证据流水线

模块：CMake、CTest、sanitizer 与 benchmark 脚本  
一句话作用：统一构建 C++20 目标、运行模块测试，并生成带环境边界的证据。

上游调用者：开发者和后续 CI。  
下游依赖：CMake、CTest、编译器、ASan/UBSan/TSan、可选 stress-ng。

输入：源码、preset、sanitizer 开关、vcan/权限和主机环境。  
输出：静态库、应用、**24** 个当前工作树 CTest 目标和 evidence 文件；正式发布数字仍以
clean evidence 为准。

运行线程：构建工具和测试进程各自运行，不是 Runtime 常驻线程。  
使用时钟：证据记录 UTC；benchmark 使用单调时钟。

拥有的资源：独立 build 目录、`evidence/` 输出，以及每次 sanitizer 调用独占的 `mktemp -d`。  
资源关闭顺序：测试各自回收；不同 sanitizer 使用不同 build 目录；脚本 `trap` 只删除本次临时目录与未 rename 的 `.tmp` 报告。

正常路径：构建系统生成进程，内核执行测试和 sanitizer runtime；报告写同目录临时文件后原子 rename。验证：`cmake --build build/linux -j && ctest --test-dir build/linux --output-on-failure`；连续两次 `./linux/scripts/run_asan_ubsan.sh` / `run_tsan.sh` 不得留下 0 字节正式报告。  
失败路径：写环境或组装报告失败时正式报告不存在（最多残留 `.tmp` 并被 trap 清理）；TSan 环境问题记 unsupported，缺 stress-ng 记 unsupported，FIFO 权限不足记 permission_denied。

为什么不用另一种方案：不建立 Linux/MCU 超级构建；firmware 不是 V1 构建依赖，证据也不能跨平台冒用。不用固定 `/tmp` 文件名，避免重跑截断与并发互踩。

我还没理解的地方：Orange Pi B4 冷启动绿灯未关；板上无 CONFIG_CAN 时 `rcrd` 常驻路径
如何与“不改内核”取舍仍待显式决策。B0–B3 已有本地证据。

## 27. Orange Pi release 安装与回滚（P3-A0）

模块：`deploy/orangepi/install_release.sh`、`rollback_release.sh`、`ORANGE_PI_BRINGUP.md`  
一句话作用：把已构建二进制装进不可变 release 目录，用 `current` 符号链接激活/回滚。

上游调用者：开发者、将来的板上 bring-up。  
下游依赖：已构建的 `rcrd`/`rcr_node_sim`/`rcr_benchmark`、`setup_vcan.sh`、git、sha256sum。

输入：build 目录、可选 `--prefix`、默认 dry-run。  
输出：`releases/<id>/bin/*`、`MANIFEST.txt`、可选更新的 `current` symlink。

运行线程：安装脚本是一次性运维进程，不是 Runtime 周期线程。  
使用时钟：MANIFEST 记录 UTC；与控制周期无关。

拥有的资源：目标 prefix 下的 release 目录与 symlink。  
资源关闭顺序：不删除旧 release；回滚只改 `current`。

正常路径：dry-run 打印计划 → `--apply` 写入 → `--activate` 或 `rollback_release.sh` 切换。  
失败路径：相对路径 prefix、装进源码树、非法 release id、覆盖已有 release、目标缺 MANIFEST/`rcrd`。

为什么不用另一种方案：不用 Docker/Ansible；不用覆盖安装；不给 `rcrd` 加 `--version`。

验证：`docs/ORANGE_PI_BRINGUP.md` §10 的临时 prefix 自测。

我还没理解的地方：（学习者填写）

## 28. Orange Pi bring-up 模板与共享矩阵（P3-A2）

模块：`BRINGUP_CHECKLIST.md`、`run_benchmark_matrix.sh`、平台 wrapper、
`collect_orangepi_host_snapshot.sh`  
一句话作用：到货前冻结“怎么操作/怎么记证据”，并把 12 格矩阵收敛成单一循环体。

上游调用者：到货后的板上操作者；ThinkPad 对照采集。  
下游依赖：已构建的 `rcr_benchmark`、可选 `stress-ng`、P2 证据 schema。

输入：平台标签、输出根目录、duration/affinity；勾选表的人工观察字段。  
输出：`evidence/{thinkpad,orangepi}_baseline/<stamp>/`；`evidence/orangepi/host_snapshot_*`；
填过的勾选表副本。

运行线程：运维脚本进程；矩阵内每格拉起一次 benchmark worker。  
使用时钟：证据 UTC；采样用 `CLOCK_MONOTONIC`。

拥有的资源：证据目录文件；不拥有 Runtime 常驻 fd。  
资源关闭顺序：单格结束即回收 stress-ng/benchmark；拒绝覆盖已有 stamp 目录。

正常路径：wrapper 设 `RCR_BENCH_PLATFORM`/`OUT_ROOT` → 共享 runner 写 environment 与
12 格 summary。验证：`bash -n` 三个脚本；本机可跑 thinkpad wrapper；orangepi wrapper
在 x86 上只证明脚本通路，不是 ARM 证据。  
失败路径：缺 binary、缺 stress-ng（该格 `unsupported`）、FIFO 权限不足
（`permission_denied`）、目标目录已存在。

为什么不用另一种方案：不用复制两套 12 格循环；不用预填 PASS；不用把产品页写成 observed。

我还没理解的地方：B4 与干净 commit 未关时，不能对外说 P3 部署完全关闭；无 CONFIG_CAN
时更不能说 `rcrd` 已常驻。`orangepi_baseline/20260805T085844Z` 是 RT0 pilot，不是
`evidence/realtime_linux/` 下的 RT1 正式基线。

## 29. Real-time Linux 证据合同（RT0）

模块：`docs/REALTIME_EVIDENCE_SCHEMA.md` / `evidence/realtime_linux/`  
一句话作用：冻结 Real-time Lab 的工具 I/O、结果枚举、证据字段和 `T/D/C/B/J` 任务模型，
并把既有 5 秒 Debug 矩阵重分类为 pilot。

上游调用者：RT1+ 采集脚本与作品集叙述。  
下游依赖：P2 `EVIDENCE_SCHEMA`、`rcr_benchmark`、可选 `cyclictest`/诊断工具。

输入：无运行时输入；合同约束后续命令行与目录字段。  
输出：冻结文档 + pilot 标注；正式 run 目录合同（RT0 不创建假报告）。

运行线程：无。  
使用时钟：合同要求记录 `CLOCK_MONOTONIC` 路径上的 wakeup lateness，不引入墙钟比较。

拥有的资源：文档与索引；不拥有板端进程。  
资源关闭顺序：无。

正常路径：阅读 schema §1/§3/§5 后能解释 pilot 测了什么、没测什么。验证：核对
`evidence/portfolio/orangepi_scheduler_20260805.txt` 含 `classification=pilot`；
`ls evidence/realtime_linux/README.md`。  
失败路径：把 pilot 写成 baseline 或与 RT1 混算提升百分比视为叙述失败。

为什么不用另一种方案：不在 RT0 新增空壳 wrapper 假装已测 `cyclictest`；不改 Runtime。

我还没理解的地方：（学习者填写）

## 30. RT1 普通内核基线 runner

模块：`run_realtime_linux_rt1.sh` / `lib/cpu_topology.sh` / `rt1_*_once.sh`  
一句话作用：按 RT1 10 格合同采集 Orange Pi 普通内核唤醒 lateness，并与 P2 的 12 格部署矩阵分离。

上游调用者：板上 root smoke/formal 入口。  
下游依赖：Release `rcr_benchmark`、可选 `stress-ng`、可写 cpufreq governor、FIFO 权限。

输入：`RCR_RT1_MODE=smoke|formal`、duration/FIFO/dirty 开关。  
输出：`evidence/realtime_linux/<stamp>_orangepi_rt1_<mode>/`（environment、topology、cells、SHA256SUMS）。

运行线程：脚本进程 + 每格一个 scheduler worker + 可选 stress-ng。  
使用时钟：证据 UTC；采样 `CLOCK_MONOTONIC`。

拥有的资源：临时证据目录、governor 原值（EXIT 恢复）。  
资源关闭顺序：停 stress → 写格摘要 → 恢复 governor → 原子 mv 发布。

正常路径：探测最高/低频核 → 设 governor → 跑 10 格（formal 另加 5ms 交叉）→ 汇总。  
验证：`bash -n`；板上 `sudo bash deploy/orangepi/rt1_smoke_once.sh`。  
失败路径：dirty 无 allow → 硬失败；缺 stress → `unsupported`；FIFO/affinity 权限 →
`permission_denied`；不自动换条件重跑。

为什么不用另一种方案：不把 RT1 塞进 `run_benchmark_matrix.sh`，避免部署 12 格与实时
学习 10 格互相污染；不硬编码 CPU6/7。

我还没理解的地方：代表格 3 次重复的自动化编排尚未做；当前 formal 单次跑完 10 格。

## 31. RT2 cyclictest 对照与归因

模块：`run_realtime_linux_rt2.sh` / `rt2_cyclictest_once.sh`  
一句话作用：用标准 `cyclictest` 复现 RT1 四代表条件，区分本仓 benchmark 与内核调度噪声。

上游调用者：板上 root 诊断入口。  
下游依赖：`cyclictest`（rt-tests）、`stress-ng`、可写 governor；可选 `timerlat`/`perf`（常 unsupported）。

输入：duration/interval/FIFO 优先级、dirty 开关。  
输出：`evidence/realtime_linux/<stamp>_orangepi_rt2_cyclictest/` 与作品集归因摘要。

运行线程：脚本 + cyclictest 测量线程 + 可选 stress-ng。  
使用时钟：cyclictest 默认 `CLOCK_MONOTONIC`；证据 UTC。

拥有的资源：临时证据目录、governor 原值。  
资源关闭顺序：停 stress → 恢复 governor → 原子发布。

正常路径：四格串行 → 解析 histogram 注释中的 Min/Avg/Max（`-N` 为 ns）。  
验证：`sudo bash deploy/orangepi/rt2_cyclictest_once.sh`。  
失败路径：缺工具 → unsupported；不能改策略 → permission_denied。

为什么不用另一种方案：不把 tracing 与低扰动矩阵同开；缺 `timerlat` 时保留排除假设，不编造 IRQ 占比。

我还没理解的地方：（学习者填写）

## 32. RT3 用户态实时编程夹具

模块：`experiments/realtime_userspace`（`rcr_rt3_mlock` / `rcr_rt3_pi_mutex` / `rcr_rt3_cycle_path`）  
一句话作用：用可撤销夹具分别观察缺页、优先级反转和周期路径分配/过载，不修改 Runtime Core。

上游调用者：学习/诊断脚本。  
下游依赖：POSIX `mmap`/`mlockall`/`pthread`/`clock_nanosleep`；FIFO 权限可选。

输入：bytes、work-ms、mode/ticks/busy-us。  
输出：机器可读 key=value；成套证据经 `scripts/run_rt3_once.sh`。

运行线程：mlock 单线程；PI 三线程同核；cycle 单线程绝对睡眠循环。  
使用时钟：cycle 用 `CLOCK_MONOTONIC`；PI 等待用 `steady_clock`。

拥有的资源：匿名映射、互斥锁、预分配缓冲。  
资源关闭顺序：unlock/join → munmap/destroy。

正常路径：`cmake` 构建 → `ctest` → `run_rt3_once.sh`。  
失败路径：mlock/FIFO 失败 → `permission_denied` 或 CTest 77。

为什么不用另一种方案：不把实验默认值写进 `PeriodicScheduler`；一次一个变量。

我还没理解的地方：（学习者填写）

## 33. RT4 PREEMPT_RT 可行性 Gate

模块：文档 Gate（无装核代码）— `docs/PREEMPT_RT_FEASIBILITY_GATE.md`  
一句话作用：在改 Orange Pi 启动内核前，先判定源码可追溯、双启动并存与可验证回退是否成立。

上游调用者：Real-time Lab 决策；人工只读探针。  
下游依赖：厂商 BSP/`boot.cmd`/包元数据；公开 `orangepi-build` 线索。

输入：板上 `uname`/config/boot 文件与 hash；包 Source 字段。  
输出：Pass / Blocked / Fallback；本次 **Blocked + Fallback**。

运行线程：无（只读 SSH 探针）。  
使用时钟：记录 UTC 时间戳即可。

拥有的资源：证据目录摘要；不拥有也不覆盖 `/boot/uImage`。  
资源关闭顺序：探针结束即断开；禁止留下半改启动项。

正常路径：采集 → 对照 Gate 清单 → 写报告 → 禁止 RT5 直到 Pass。  
失败路径（本义）：缺源码闭环 / 无第二启动项 → Blocked；仍允许 ThinkPad 方法 Fallback。

为什么不用另一种方案：不“先刷 RT 镜像再补证据”；不把 UART 能救板当成双启动 Pass。

我还没理解的地方：（学习者填写）

## 34. RT6 分段时延夹具

模块：`experiments/realtime_segmented`（`rcr_rt6_segments`）  
一句话作用：在软件 peer 路径上分别报告 wakeup/callback/queue/io_ack/e2e，证明空 callback
p99 不能代替路径延迟。

上游调用者：学习/证据脚本。  
下游依赖：`rcr::PeriodicScheduler`、`EventFd`、自写 epoll；不启动 `CanIoLoop`。

输入：`--mode` / `--ticks` / `--period-us` / `--busy-us` / 可选 `--fifo`。  
输出：各段 min/p50/p99/max、drops、deadline_misses；成套证据经 `run_rt6_once.sh`。

运行线程：周期生产者 + I/O 消费者。  
使用时钟：全程 `CLOCK_MONOTONIC`（`SchedulerTick` + `monotonic_now_ns`）。

拥有的资源：SPSC 环、两个 eventfd、epoll fd、完成样本向量。  
资源关闭顺序：停调度 → join 周期线程 → stop eventfd → join I/O → close epoll。

正常路径：`cmake` → `ctest` → `rt6_orangepi_once.sh`。  
失败路径：FIFO 权限不足 → `permission_denied` / 77；队列满 → `drops` 可见。

为什么不用另一种方案：不改 Runtime Core；不用板上无驱动的 SocketCAN 作退出条件。

我还没理解的地方：（学习者填写）

## 35. RT7 Real-time Lab 收口

模块：文档收口 — `evidence/portfolio/orangepi_rt7_wrapup_20260805.md`  
一句话作用：汇总普通内核已测结论、明确未做的 PREEMPT_RT 对照，并固定面试证据等级与
不能声称清单。

上游调用者：作品集叙述 / 面试准备。  
下游依赖：RT0–RT4、RT6 摘要与 Gate 文档；不产生新内核测量。

输入：既有 portfolio 与 schema。  
输出：因果图、复跑索引、证据等级表、负面结果、改写后的作品集表述。

运行线程：无。  
使用时钟：无新采样。

拥有的资源：仅文档。  
资源关闭顺序：不适用。

正常路径：阅读收口 → 按等级表回答面试 → 提交文档。  
失败路径（叙述）：若把“收口”说成“已对比 RT 内核 / 硬实时”即越界。

为什么不用另一种方案：不补跑伪 RT 对照充数；不把 draft 计划原文里的 PREEMPT_RT 对比
写成既成事实。

我还没理解的地方：（学习者填写）

## 36. Headless Test Runner（Workbench Foundation T0）

模块：`rcr::workbench::TestRunner`（`services/test_runner.hpp`）
一句话作用：在不依赖 Qt、CAN 或设备的前提下，固定设备测试的准备、执行、判定、取消和
清理合同。

上游调用者：当前单元测试；未来 headless CLI 或 Qt worker。
下游依赖：`rcr::Result`、`monotonic_now_ns()`；不依赖 Qt。

输入：run id、固定 C++ `TestCaseDefinition`、timeout、可注入 clock。
输出：包含 measurements、criteria、diagnostics、reason、outcome、error、墙钟/单调时间和
cleanup status 的 `TestResult`。FAIL/ERROR 在返回前封口，保证这四类证据都在。

运行线程：同步运行在调用线程；`request_cancel()` 可从另一线程调用，Runner 不创建线程。
使用时钟：默认 `CLOCK_MONOTONIC`；测试使用 fake clock。

拥有的资源：Runner 拥有 run 状态和取消握手；case 拥有未来 I/O/DUT 资源。
资源关闭顺序：进入 Prepare 后，任何显式结果路径都调用 Cleanup；具体 I/O 顺序由 case 定义。

正常路径：validate → Prepare → Execute → Evaluate → Cleanup → PASS/FAIL。验证：
`ctest --test-dir build/workbench-phase1 -R '^test_workbench_runner$' --output-on-failure`。
失败路径：非法合同为 ERROR；criteria 不满足为 FAIL；取消为 ABORTED；deadline/step error 为
ERROR；Cleanup 失败单独记录且不能保留 PASS。

为什么不用另一种方案：不把生命周期放进 MainWindow；只有一个计划中的真实 CAN case 时
不建立 DSL、device plugin 或 generic Transport。

我还没理解的地方：未来 Direct CAN session 的跨进程独占和 `rcrd` 冲突检测仍未定义。

## 37. Runtime Application Adapter（Workbench Phase 1）

模块：`rcr::workbench::RuntimeApplicationAdapter` / `application/application_model.hpp`
一句话作用：把 Runtime 内部快照与命令入口投影成无 Qt、无 SocketCAN 类型的应用层合同。

上游调用者：当前单元测试；未来 Qt model/controller 或 headless CLI。
下游依赖：具体 `RuntimeDaemon`；实现文件依赖 Runtime/CAN V1，公开 model 只依赖标准库。

输入：activate/deactivate/clear fault、带 session/sequence/相对有效期的数字输出请求。
输出：`CommandReply`、`RuntimeTelemetrySnapshot` 和当前观察产生的 `DiagnosticEvent`。
`DeviceView` 投影 `input_bits` 与只读 `last_output_mirror`；CellReady 由
`CellReadyMapper` 在 Workbench 应用层计算，不在 Adapter / Runtime Core 里。

运行线程：不创建线程；在调用者线程同步映射线程安全 snapshot/command result。
使用时钟：`CLOCK_MONOTONIC`，用于 observation timestamp、heartbeat age 和命令 deadline。

拥有的资源：不拥有 daemon、fd、watchdog、fault、scheduler 或 transport；仅 non-owning 引用和
配置副本。
资源关闭顺序：无；调用者必须保证 `RuntimeDaemon` 生命周期长于 Adapter。

正常路径：snapshot → DTO projection → future Qt model；command → 参数预检 → Runtime admission。
验证：`test_workbench_runtime_adapter`。
失败路径：非法输入在应用边界拒绝；合法输入保留 Runtime 的 NotOpen/Rejected/Timeout 等结果；
时钟失败成为 command error 或 snapshot diagnostic。

为什么不用另一种方案：只有一个 Runtime 实现，不创建 `IRuntimeService`/Factory；不使用 QObject
避免 Runtime 邻接层引入 Qt；IPC 留到进程协议和版本合同明确后实现。

我还没理解的地方：未来 IPC 的消息版本、订阅背压和 daemon/client 重连合同尚未设计。

## 38. CAN Communication Health Test（Workbench Phase 2）

模块：`rcr::workbench::CanCommunicationHealthTest`（`services/can_health_test.hpp`）
一句话作用：在固定观察窗口内只读采样 Runtime application snapshot，把 heartbeat、队列和
故障事实判定为可区分的 PASS/FAIL/ERROR/ABORTED 结果。

上游调用者：当前 headless 单元/可选 vcan 集成测试；未来 Qt worker 或 headless CLI。
下游依赖：`RuntimeApplicationAdapter`、`TestRunner`；间接观察 RuntimeDaemon，不直接依赖或
拥有 SocketCAN fd。

输入：显式 evidence class、观察窗口、采样周期、总 timeout 和各计数/age 阈值。
输出：内存 `TestResult`，包含六项 measurement、八项 criterion、communication/device/test
诊断、environment/parameters、outcome、reason 和 cleanup status。

运行线程：同步运行在调用者线程；Runtime scheduler 与 CAN I/O 继续在各自线程。
使用时钟：Runner 默认 `CLOCK_MONOTONIC`；单元测试注入 fake clock/wait，避免 wall-clock 抖动。

拥有的资源：不拥有 daemon、CAN socket、设备 session 或 Runtime fault；仅在一次 run 栈上拥有
baseline/latest snapshot 和统计值。
资源关闭顺序：本 case 无独占资源，仍经过 Cleanup；fixture 负责先停 Runtime、再停 simulator。

正常路径：Prepare 检查 Runtime/evidence → Execute 周期采样 → Evaluate 显式阈值 → Cleanup →
PASS。验证：`test_workbench_can_health`；有可用权限时再跑 `test_workbench_can_health_vcan`。
失败路径：阈值不满足为 FAIL；前置条件或计数器倒退使证据无效为 ERROR；取消为 ABORTED；
缺少 PF_CAN 权限的集成目标为 Skipped，不能记 PASS。

为什么不用另一种方案：不让 Workbench 再开一个可写 CAN socket，因为当前没有跨进程
authority/lease 合同；只读 Runtime 快照能闭合健康诊断，同时维持单一 transport owner。

我还没理解的地方：未来 IPC 后快照版本、断连语义，以及 Direct CAN bench 独占 lease 尚未设计。

## 39. Workbench Result Writer（Phase 3）

模块：`rcr::workbench::ResultWriter`（`services/result_writer.hpp`）
一句话作用：把一次 `TestResult` 原子写成固定 schema 的 JSON 完整证据和一行 CSV 索引。

上游调用者：当前单元/健康测试；未来 headless CLI 或 Qt Results 页。
下游依赖：`TestResult`、POSIX `open`/`write`/`fsync`/`rename`、`OwnedFd`；不依赖 Qt 或 JSON 库。

输入：已封口的 `TestResult` 和目标目录。
输出：`<run_id>.json` 与 `<run_id>.csv` 路径，或显式 `InvalidArgument`/`Busy`/`IoError`。

运行线程：调用者线程同步写文件；禁止放进 Runtime 周期回调。
使用时钟：不采样时钟；只序列化结果里已有的单调时间和墙钟字段。

拥有的资源：写临时文件期间拥有一个 fd；成功 rename 后不再持有路径。
资源关闭顺序：写完 → `fsync` → close → rename；失败则删除 `.tmp`，CSV 失败时回滚已写出的 JSON。

正常路径：校验 run_id/FAIL 证据 → 创建目录 → 原子写 JSON → 原子写 CSV。验证：
`test_workbench_result_writer`；CAN Health 失败结果复用同一 writer。
失败路径：缺 reason/criteria/measurement/diagnostic 拒写；不安全 run_id 拒写；已有最终文件
返回 `Busy`；写/同步/改名失败返回 `IoError`，不留下看似完整的最终文件。

为什么不用另一种方案：不把写文件放进 TestRunner，以免生命周期测试依赖文件系统；不引入
第三方 JSON 或数据库，因为当前只有一种固定结果记录。

我还没理解的地方：未来多 run 目录布局、跨主机比较同一 schema 的工具，以及 Qt 浏览页尚未实现。

## 40. Workbench Clean Evidence Runner（Phase 3.5）

模块：`linux/scripts/run_workbench_clean_evidence.sh` + vcan test persistence hook
一句话作用：从干净提交一次性生成可追溯的 Workbench 软件 Gate 证据。

上游调用者：开发者或 CI 的显式验收命令。
下游依赖：Git、CMake/CTest、vcan0、现有 Workbench tests、ResultWriter、ASan/UBSan、SHA-256。

输入：干净工作树、CAN interface 名称、可选 build 目录环境变量。
输出：时间戳证据目录，包含环境、接口、普通/消毒器测试日志、三组 JSON/CSV 和哈希清单。

运行线程：shell 同步编排多个构建/测试进程；不进入 Runtime 周期线程。
使用时钟：UTC 只命名和标注证据；Runtime/TestRunner 仍使用单调时钟判定 deadline。

拥有的资源：脚本拥有临时目录；各测试各自拥有 Runtime/simulator 生命周期。
资源关闭顺序：测试 cleanup → 进程退出 → 计算哈希 → 临时目录整体发布；失败由 trap 清理临时目录。

正常路径：clean check → build → 23 CTest → vcan 三场景落盘 → ASan/UBSan → hash → publish。
失败路径：dirty、无 vcan、权限不足、测试失败或写入失败均返回非零，不生成完整正式目录。

为什么不用另一种方案：当前没有生产 CLI 需求；默认关闭的 test persistence hook 避免新增一套
daemon/CAN composition，同时仍使用真实 Runtime-connected 路径。

我还没理解的地方：未来 CI artifact 保留期限、签名和跨主机结果索引尚未设计。

## 41. Optional Qt6 Device Workbench（Phase 4）

模块：`linux/tools/qt_device_workbench/{app,controller,ui}/`
一句话作用：用最小 Qt Widgets 界面消费既有 Runtime snapshot、CAN Health 和 ResultWriter。

上游调用者：现场/开发者本地 UI，或 offscreen `--run-health-once` smoke。
下游依赖：Qt6 Core/Widgets、`rcr::workbench`、同进程 RuntimeApplicationAdapter；Qt 不反向进入 core。

输入：`--can`、`--node-id`、结果目录、Run/Cancel UI action。
输出：Overview、criteria、diagnostics、结果路径，以及已有 schema 的 JSON/CSV。

运行线程：UI event loop + Runtime 自有线程 + 一个健康测试 worker QThread。
使用时钟：QTimer 100 ms 刷新；测试 deadline/heartbeat 用 monotonic；墙钟只生成 run id。

拥有的资源：main composition 拥有 RuntimeDaemon；Controller 拥有 timer/thread；worker 拥有
TestRunner；MainWindow 只拥有 widgets。
资源关闭顺序：停 timer → cancel → thread quit/wait → 销毁 adapter → daemon stop。

正常路径：Qt signal → worker health → queued TestResult → tables → ResultWriter 路径。
失败路径：启动错误退出；Test FAIL 显示；持久化错误单独显示；不升级 Runtime fault。

为什么不用另一种方案：快 snapshot 不需要线程；慢测试不能阻塞 UI；IPC 合同尚未冻结，不在
本 Phase 扩大范围。

已验证：clean commit `834ec899` 上 Qt6 6.4.2 ON build、23/23 CTest 和 offscreen VCAN
health 通过。当前 dirty tree 又加入显式 `--evidence vcan|physical` 和 QtTest；physical Qt
Health 尚未在 Orange Pi 上运行。IPC 和 crash containment 未实现。

学习入口（零基础）：[workbench/NOTES.md](workbench/NOTES.md)。
分层地图：[workbench/README.md](workbench/README.md)。

## 42. Mock Actuator 01 Profile（Phase 5A）

模块：`rcr::workbench::MockActuatorProfile`（`profile/mock_actuator_profile.hpp`）+ Qt Actuator 01 page
一句话作用：以确定性单轴 Mock 学习和验证 Enable、Homing、Jog、停止、限位与 fault recovery。

上游调用者：headless unit test；Qt WorkbenchController。
下游依赖：标准 C++/application DTO；不依赖 RuntimeDaemon、SocketCAN、Qt 或物理设备。

输入：typed actuator command、rad/rad/s 参数、显式 elapsed。
输出：`ActuatorSnapshot` 和 `ActuatorCommandReply`，证据固定为 `MOCK / ISOLATED`。

运行线程：对象不建线程；测试线程或 Qt UI thread 调用。
使用时钟：模型不读墙钟；调用者传 elapsed。lease 看完整 elapsed，积分步长最多 50 ms。

拥有的资源：Controller 独占一个 profile；没有 fd、worker 或 heap registry。
资源关闭顺序：停 renewal/model timer → 销毁 Controller/profile；没有真实输出需要回收。

正常路径：Enable → Home → Ready → velocity/Jog → Stop → Ready。
失败路径：非法转移 reject；deadman/max duration 停 Jog；limit/tracking/blocker → Fault；安全 Reset
回到 Disabled 且不重放目标。

为什么不用另一种方案：一个 Mock 不足以证明通用 device interface；数字输出 mailbox 不能表达
运动命令；轻量显式 tick 不需要 QThread。

当前证据：历史 dirty-tree headless 13 场景、Qt OFF/ON 24/24、ASan/UBSan 6/6、offscreen
smoke 通过。A2 Runtime admission、Workbench physical actuator 和 clean evidence 未完成；
仓库另有 STM32F103 双向 physical CAN、PC13、SG90 双位置目视动作和仲裁诊断，但它不经过
本 Mock 或 Qt Workbench。

## 43. Mock Modbus I/O Profile（M1/M2 local Gate 已关）

模块：`rcr::workbench::MockModbusIoProfile`（`profile/mock_modbus_io_profile.hpp`）+ Qt Modbus I/O page
一句话作用：在没有 MR0-IOR08 手册和实物链路时，确定性验证 slave scan、4 DI、4 DO
requested/confirmed 和失败恢复。

上游调用者：headless unit test；Qt WorkbenchController。
下游依赖：标准 C++/application evidence DTO；不依赖 RuntimeDaemon、SocketCAN、Qt 或 Serial。

输入：begin/complete scan、显式 Mock DI injection、DO/All OFF request、下一笔 fault outcome、
调用者提供的 monotonic ns。
输出：`ModbusIoSnapshot` 和 `ModbusIoCommandReply`，证据固定为
`MOCK / NO PHYSICAL RS485`。

运行线程：对象不建线程；测试线程或 Qt UI thread 调用，真实 I/O 不在本模块。
使用时钟：不读墙钟；拒绝倒退的调用者单调时间。scan 的 TIMEOUT 是配置结果，不是物理计时。

拥有的资源：Controller 独占 profile；只有 fixed arrays/vector/string，没有 fd、worker 或设备。
资源关闭顺序：无；销毁对象即释放内存，不存在要 neutralize 的真实 relay 输出。

正常路径：UNKNOWN → SCANNING → COMPLETE；primary ONLINE；DI injection 发布 snapshot；DO success
令 confirmed 跟随 requested。
失败路径：invalid channel reject；timeout/exception/rejected 不改 confirmed；下一次 successful scan
恢复 device state；单调时间倒退拒绝。

为什么不用另一种方案：真实 backend 尚不存在，不建通用接口；手册未确认，不引入 QtSerialBus、
libmodbus 或自研 RTU；CAN MCP2515 overlay 不承担 UART/RS-485 ownership。

验证：`test_mock_modbus_io_profile` 覆盖 headless 状态/失败、scan ERROR 和显式恢复；
`test_qt_workbench` 覆盖页面标签、scan、DI/DO signal/slot、timeout/exception/rejected、All OFF
与 invalid channel。它们只关闭 local/dirty Mock Gate，不是 clean 或 physical RS-485 证据。

已确认硬件：Waveshare 普通版 `RS485 CAN HAT`；MCP2515 CAN 侧已有独立双向 CAN V1、PC13
输出、SG90 无负载双位置目视动作和专用仲裁诊断。RS-485 侧为 SoC UART + SP3485；can2 已将
UART7 启用为 `/dev/ttyS7`，live DT/驱动/占用检查通过，但尚未发送物理 RS-485 数据。

我还没理解的地方：到货设备与 MR0-IOR08 手册修订的一致性、RSE 实际配置、A/B/GND、
终端/偏置、电气收发和最终库选型仍待实物 Gate；不能用 tty 枚举替代这些结论。

## 44. Remote Workbench 控制面帧与 loopback endpoint（M1）

模块：`remote_frame` + `RemoteControlEndpoint`（`application/remote_frame.hpp`、
`application/remote_control_protocol.hpp`）
一句话作用：在 TCP 字节流上用有界二进制帧做 HELLO / HEARTBEAT / GET_STATUS，证明
PC client 与 Runtime 应用边界可以先在 localhost loopback 验证，而不把 Runtime 私有结构上网。

上游调用者：headless unit test；`RemoteRuntimeClient` / Qt Connection（M2）。
下游依赖：应用 DTO / `RemoteStatusView`；不依赖 Qt、SocketCAN、正式 `rcrd` 或真实网卡。

输入：字节流片段（可半包/粘包）；fixture `RemoteStatusView`；可选关闭 HEARTBEAT 应答。
输出：编码后的回复帧；会话状态 WAITING_HELLO / ESTABLISHED / FAULTED；malformed 计数。

运行线程：对象不建线程；测试线程或未来 worker 线程调用 `push_bytes`。
使用时钟：status 里的 monotonic 字段由调用方/fixture 提供；本模块不读墙钟做 timeout。

拥有的资源：有界 RX 缓冲与计数器；没有 socket fd（M1 是 in-memory stream）。
资源关闭顺序：`reset_session()` 清解析器与会话；销毁对象即释放内存。

正常路径：HELLO → HELLO_ACK（`LOOPBACK`）→ HEARTBEAT_ACK / STATUS（64 字节固定投影）。
失败路径：invalid magic / version / oversize / bad CRC / overflow；GET_STATUS before HELLO →
ERROR；停止 heartbeat 应答时 outbound 为空，由上层观察 timeout。

为什么不用另一种方案：不选 gRPC/ZMQ（依赖重、面试收益不在本仓）；不选 JSON 变长报文（难测
半包边界）；不把 `DaemonSnapshot` `memcpy` 上网（泄漏私有布局）；不先改正式 `rcrd`。

验证：`test_remote_frame`、`test_remote_loopback`（Qt OFF）。证据等级
`LOOPBACK / NO PHYSICAL PC-ARM`；不是物理 ThinkPad↔Orange Pi，也不是 crash isolation 产品验收。

我还没理解的地方：真实 TCP worker 的 Qt 线程亲和与关闭顺序；物理跨机丢包/NAT 行为
（另开 Gate）；COMMAND/lease 恢复合同尚未定义。

## 45. RemoteRuntimeClient 与 Qt Connection 页（M2）

模块：`RemoteRuntimeClient` + `WorkbenchController` remote 编排 + `MainWindow` Connection 页
一句话作用：在保留 Local Overview/Mock/Health 的同时，用显式 HELLO 会话展示 Remote
LOOPBACK 应用边界；UI 只发请求和显示 DTO。

上游调用者：Qt Connection 页按钮；QtTest。
下游依赖：`RemoteControlEndpoint`（进程内）；`RuntimeApplicationAdapter::snapshot` 投影为
`RemoteStatusView`。不依赖 `QTcpSocket` / UDP。

输入：Select Local / Select Remote LOOPBACK / Connect / Disconnect；100 ms timer 在已连接时
轮询 HEARTBEAT + GET_STATUS。
输出：`RemoteConnectionSnapshot`（banner/mode/peer/session/heartbeat/STATUS/error）。

运行线程：M2 全在 UI 线程（与 Modbus Mock 相同），因为没有真实 socket 阻塞。未来 TCP 必须
迁到 worker，且 socket 仍不得进入 `MainWindow`。
使用时钟：`QElapsedTimer` 单调 ns 只填 HEARTBEAT payload，不是硬实时周期。

拥有的资源：Controller 独占 endpoint/client；Window 只持有标签/按钮指针。
资源关闭顺序：Controller 析构先 `disconnect_session`，再停 timer / quit worker。

正常路径：Local 默认 → Remote LOOPBACK → Connect → ESTABLISHED → heartbeat/status 递增。
失败路径：未 Connect 时 Connect 按钮按模式灰显；停止 heartbeat 应答计入 missed；Disconnect
回到 WAITING_HELLO。Overview 的 `SOCKETCAN / VCAN` 不被改写成 LOOPBACK。

为什么不做 UDP / 真实 TCP 这一轮：Gate 允许跳过 UDP；先钉 UI 合同与 Local 共存，再加
socket/worker 复杂度。真实跨机另开物理 Gate。

验证：`test_remote_runtime_client`；`test_qt_workbench::routesRemoteLoopbackConnectionPage`。
不能声称：物理 PC–ARM、UDP plane、Qt crash isolation、COMMAND。

## 46. Modbus RTU codec 与 Physical I/O service

模块：`modbus_rtu` + `PhysicalModbusIoService` + `PosixSerialPort`
一句话作用：把 2026-08-15 在 `/dev/ttyS7` 上验证过的 FC02 读、以及 FC05 写单线圈做成
Qt-free 主站语义；probe 成功才标 ONLINE，写成功才标 confirmed。

上游调用者：`rcr_modbus_rtu_agent`、注入 transact 的单测。
下游依赖：POSIX termios 或测试注入的 `RtuTransact`；不依赖 Qt、RuntimeDaemon、SocketCAN。

输入：slave/baud/port、probe / read_inputs / write_output / write_all_outputs_off。
输出：`ModbusIoSnapshot`（PHYSICAL 证据、DI 位、DO requested/confirmed、TX/RX hex、RTT）。

运行线程：调用者线程阻塞完成一笔事务；GUI 禁止直接调用 POSIX transact。
使用时钟：`CLOCK_MONOTONIC` 只填 RTT；串口 timeout 默认 200 ms。

拥有的资源：`OwnedFd` 串口；注入路径不打开 tty。
资源关闭顺序：`disconnect()` close fd。

正常路径：encode FC02 `01020000000879cc` → 合法应答 → DI0–3；FC05 回显后 confirmed。
2026-08-16 板上 live：DO0 ON `01050000ff008c3a` / OFF `010500000000cdca`（无市电负载）。
失败路径：timeout 标 TIMEOUT，不把 DI 涂成 ON，也不把 requested 写成 confirmed；CRC/功能码错误标 ERROR。
ALL OFF 连发 FC05（FC0F 尚未 live-verify），中途失败立即停、不重试。

为什么不用 QtSerialBus/libmodbus/socat：Qt 不能进 `rcr_workbench`；已验证功能码不值得新
依赖；字节转发会把 3.5 字符间隔丢到 Wi-Fi 上。

验证：`test_modbus_rtu`、`test_physical_modbus_io_service`。物理 PASS 仍要板上 agent。

## 47. Modbus commissioning agent（PC Qt ↔ ARM 主站）

模块：`modbus_agent_protocol` + `ModbusAgentClient`/`Server` + `rcr_modbus_rtu_agent`
一句话作用：ThinkPad 只发有界 commissioning 请求，Orange Pi 在本地做完整 RTU 事务。
Magic `RCRM`，与 Runtime Remote `RCRB` / HELLO 控制面分开。同一 TCP 会话上走 Probe /
ReadDi / WriteDo / AllOff，不每 500 ms 重连。

上游调用者：Qt `ModbusAgentWorker`；localhost 单测。
下游依赖：`PhysicalModbusIoService`。

输入：TCP Probe/ReadDi/WriteDo/AllOff 帧；agent CLI `--serial/--baud/--slave/--listen`。
输出：对应 Ack 快照载荷（含 DI 与 DO requested/confirmed）。

运行线程：agent 单线程 accept 后循环处理直到客户端断开；client 在 Qt worker 线程阻塞。
使用时钟：TCP 与串口 timeout 均有界，无无限重试。空闲 5 s 结束会话。

拥有的资源：listen/client socket；板上才拥有 tty。
资源关闭顺序：client `shutdown`+`disconnect` 可从 UI 线程唤醒阻塞 recv；server close listen fd。

正常路径：listen 127.0.0.1 或 `0.0.0.0:5740` → Probe → FC02/FC01 → ONLINE → 轮询/写线圈。
失败路径：未连接不伪装成 MOCK；错误帧拒绝；RTU timeout 仍回 Ack，快照里标 TIMEOUT。

为什么不复用 Remote 控制面：Operator↔Runtime 与 commissioning↔现场 I/O 语义不同。

验证：`test_modbus_agent_loopback`；`test_qt_workbench` Physical Probe / DI / DO。

## 48. Qt Modbus backend 选择与 Probe / 轮询 worker

模块：`WorkbenchController` Physical/Mock 编排 + `ModbusAgentWorker` + Modbus I/O 页
一句话作用：显式选择 PHYSICAL，把阻塞 TCP 丢到独立 QThread；约 500 ms DI 轮询不是控制环；
DO 只在 ONLINE 时使能，confirmed 只来自 FC05 成功。

上游调用者：Qt 按钮；QtTest。
下游依赖：`ModbusAgentClient`；Mock profile 仍用于回归。

输入：MOCK/PHYSICAL、agent `host:port`、Probe/Disconnect、DO 勾选、ALL OFF。
输出：banner `PHYSICAL MODBUS RTU` 或 `MOCK / NO PHYSICAL RS485`；device state 文本不靠颜色。

运行线程：UI 发 queued request；worker 阻塞 TCP；结果 queued 回来。忙则跳过轮询，不排队。
使用时钟：连接 500 ms、事务 1000–2000 ms；轮询 500 ms CoarseTimer。

拥有的资源：第二根 QThread + worker QObject。CAN Health 线程不动。
资源关闭顺序：先停 poll timer、shutdown client，再 quit/wait 两根线程。

正常路径：PHYSICAL → Probe → ONLINE → DI 边沿更新 → DO requested 先亮、confirmed 后亮。
失败路径：Physical 拒绝 Mock DI injection；timeout 后拒绝新 DO（需显式 Probe，不重放）；
不静默回退 Mock。

验证：原有 Mock QtTest + Physical localhost Probe/DI/DO。尚未关闭板上继电器录屏。

## 49. STM32 PA0 到位去抖 → `input_bits`

模块：`input_debounce.c` + `rcr_platform_target_sensor_*` + `main` 采样
一句话作用：把对射红外 DO 的 raw 电平归一成 CAN V1 `NodeStatus.input_bits` bit0，供
Linux 监督器观测；不走 EXTI，不用 `delay()` 或“已发 PWM”冒充到位。

上游调用者：STM32 `main` 循环；主机 `test_logic.c`。
下游依赖：既有 100 ms `publish_status`；不改 TIM1 / bxCAN / IWDG / SysTick 所有权。

输入：PA0 IDR 高低（`raw_high`）；编译期极性；`rcr_platform_millis()`。
输出：稳定后的 `node.input_bits`（仅 bit0）；其余位保持 0。
2026-08-16 极性冻结为 ACTIVE_HIGH（遮挡 = PA0 HIGH）。

运行线程：主循环轮询，与 CAN 泵送同一上下文；无第二套调度。
使用时钟：SysTick 1 ms；去抖窗口 20 ms，用有符号环差，与 lease 到期同一时间模型。

拥有的资源：GPIOA PA0 内部上拉输入；去抖状态在 main 栈上。
资源关闭顺序：复位后重新 init，默认未到位。

正常路径：`raw_high` → `rcr_target_sensor_active` → 连续 20 ms 一致才提交 bit0。
验证：`ctest --test-dir build/stm32f103-host`。
失败路径：短毛刺不改稳定值；上电默认 bit0=0。UNSET 仅用于未测量极性。

为什么不用另一种方案：不新增 CAN 消息；不用 EXTI；不把 GPIO 极性写进 `node.c`；
不选 PB14（默认 TIM1_CH2N，与已运行的 TIM1 耦合）。CAN/Application 只看
`POSITION_REACHED`，不知道 PA0 active-high / active-low。

引脚冻结：`PA0 = TARGET_SENSOR_DO`（VCC→3.3V，GND→GND，DO→PA0，AO 不接）。
不能声称：极性已测量、实物红外边沿 PASS、或外接灯/现场负载闭环。

我还没理解的地方：（学习者填写）

## 50. CellReadyMapper（应用层单元决策）

模块：`rcr::workbench::CellReadyMapper`
一句话作用：把已解码的机器人节点到位观测映射成 CellReady，并只在边沿请求 MR0 DO0。

上游调用者：`rcr_cell_app` 主循环；本机 `--can vcan0` 时 Qt `WorkbenchController`。
下游依赖：现有 `ModbusAgentClient.write_output(0)` / agent FC05；不进入 `rcrd`。

输入：`RuntimeTelemetrySnapshot`（online、Active、`input_bits` bit0、fault=0）。
输出：`position_reached`、`cell_ready`、可选 DO0 ON/OFF 请求。

运行线程：边缘 `rcr_cell_app` 主循环；本机 vcan Qt 仍可在 UI 快照周期 tick。真正的 RTU
写在 agent 进程。
使用时钟：不另读时钟；跟随现有 snapshot / agent 超时。

拥有的资源：边沿状态 `armed_` / `last_ready_`。不拥有 CAN fd、tty、继电器。
资源关闭顺序：Modbus 掉线 `note_modbus_offline()`，Probe 后不重放历史 DO。

正常路径：CAN bit0 → evaluate → 边沿 → requested → confirmed。
验证：`ctest --test-dir build/qt-on -R test_cell_ready_mapper`。
失败路径：Hold/Fault/offline 不是 CellReady；decoder 不写线圈；无外接 LED 时只声称
MR0 DO0 requested/confirmed，不写“现场灯已亮”。

为什么不用另一种方案：不把单元灯策略塞进 Runtime Core；不在 STM32 发 LIGHT_ON。
`--cell-peer` 模式下 Qt 不得再跑本地 mapper。

我还没理解的地方：（学习者填写）

## 51. `rcr_cell_app` 与 CEL1 工程站协议

模块：`cell_app_protocol` + `CellAppClient`/`Server` + `rcr_cell_app`
一句话作用：把 CAN 所有权和 CellReady 闭环留在 Orange Pi；ThinkPad Qt 只做工程站。

上游调用者：ThinkPad `rcr_qt_device_workbench --cell-peer`；localhost 单测。
下游依赖：`RuntimeDaemon`、`RuntimeApplicationAdapter`、`CellReadyMapper`、localhost
`ModbusAgentClient`。不进 `rcrd` CLI，不扩张 Remote HELLO。

输入：CEL1 GetStatus / Activate / SubmitOutput；CLI `--can/--modbus/--listen/--evidence`。
输出：80 字节 status 快照；CommandReply；边沿 DO0 写到 agent。

运行线程：daemon I/O + 周期线程；cell_app 主循环 tick mapper 并 `server.poll`。
使用时钟：CAN 周期仍是 daemon 的 `CLOCK_MONOTONIC`；TCP poll 约 20 ms，不是控制环。

拥有的资源：can0（演示唯一写者）、listen `:5750`、localhost agent 客户端。tty 仍归 agent。
资源关闭顺序：先 disconnect Modbus 与 listen，再 `request_stop` + `wait_and_stop`。
不把 `wait_and_stop` 放到并行线程去和 snapshot 抢 `runtime_`。

正常路径：boot 等待 Activate → TARGET → bit0 → CellReady 边沿 → DO0。关掉 Qt 后 mapper 仍在。
失败路径：未连接 client 不编造状态；Modbus 掉线不重放 DO0；不静默 Mock。CRC 错回 Error。

为什么不用另一种方案：不把 Modbus 放进 `rcrd`；不把 mapper 放进 ThinkPad Qt；不复用 RCRM
或 Remote 控制面。CAN 输出仍只接受 u16 session/sequence 与 u8 mask/values，CEL1 载荷按
`DigitalOutputRequest` 全宽编码，超范围由 Adapter 拒绝。

验证：`test_cell_app_protocol`、`test_cell_app_loopback`。物理 15 项仍是 NOT RUN。

我还没理解的地方：（学习者填写）
