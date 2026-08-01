# C/C++、Linux Runtime 与面试知识库

状态：Living document  
适用对象：大致了解 C/C++ 和操作系统概念、但尚不能完整解释 Linux Runtime 调用链的开发者。

这份文档不是语法大全，也不要求先学完再写代码。推荐方法是：先读直觉模型，再在本仓
找到对应代码，运行观察实验，最后尝试不用术语堆砌地口述面试答案。

## 1. 怎样使用这份知识库

每个主题按三种证据等级标记自己：

| 等级 | 含义 | 面试时可以怎样说 |
|---|---|---|
| 理解过 | 能解释概念和主要取舍，但没有亲手实现 | “我理解其机制和适用边界，但本项目尚未使用” |
| 使用过 | 代码中实际使用，且单元/集成测试通过 | “我在该调用链中使用，并用这些测试验证” |
| 测量过 | 在明确平台、负载和配置下保存了可复现数据 | “在这个内核和负载条件下测得……，不推广到其他平台” |

面试前不要只背结论。对每个主题练习回答五句话：问题是什么、方案是什么、内核做什么、
失败怎样表现、证据在哪里。

## 2. 一分钟项目说明

可以先用下面这段结构组织自己的语言：

> 这个项目的目标是部署一个 Orange Pi 上的 ROS-free Linux 边缘 Runtime。它不做电机 PID，
> 而是练习周期监督、SocketCAN fd 事件循环、状态机、watchdog、trace 和 systemd 部署。
> 当前已经完成 Linux Core 与 CAN V1 无状态 codec：使用 `CLOCK_MONOTONIC` 和绝对时间睡眠，
> 申请 `SCHED_FIFO` 失败时能显式降级；普通输出通过带 session、sequence 和 deadline 的
> latest-wins mailbox；线级 8-byte 消息经显式大端编解码，golden vectors 已单测通过。
> 独立模拟器、daemon 和 Orange Pi 实测仍按阶段推进。因此现有证据证明普通 Linux Core
> 与协议编解码的软件行为，不是硬实时或功能安全认证。

面试官继续追问时，再展开后面的调用链和取舍，不要一开始罗列所有 API。

## 3. 先建立操作系统直觉

### 3.1 用户态、内核态与系统调用

应用代码运行在用户态（user space），不能直接操作网卡、调度器或内核等待队列。调用
`socket`、`read`、`write`、`epoll_wait`、`clock_nanosleep` 时，会通过系统调用
（system call）请求内核完成工作。

可以把边界理解为：

```text
C++ 对象/业务规则
      ↓ 函数调用
libc / pthread 薄封装
      ↓ system call
Linux 内核：调度、时钟、fd、网络协议栈
      ↓
驱动或 vcan
```

本项目没有“实现 Linux 内核”。准确说法是：使用并验证 Linux 提供的调度、时间和 I/O
接口，并处理它们的权限、错误和生命周期。

### 3.2 进程与线程

进程（process）提供虚拟地址空间和 fd 表；同一进程的线程（thread）共享大部分内存和
fd，但各自有栈、寄存器和调度属性。Linux 内核实际调度的是线程，所以
`pthread_setschedparam(pthread_self(), SCHED_FIFO, ...)` 只改变调用它的周期线程，
不是自动改变整个进程。

当前 `PeriodicScheduler` 独占一个 `std::thread`。Application 线程发布事件和命令，周期
线程检查 deadline/watchdog。未来 I/O 线程等待 CAN、停止事件和信号。没有线程池，因为
当前 fd 数量少且顺序与关闭行为比吞吐量更重要。

### 3.3 文件描述符不是文件对象

文件描述符（file descriptor, fd）是进程 fd 表中的一个小整数，指向内核对象。socket、
epoll、eventfd、signalfd 和 timerfd 都能表现为 fd，所以可以被统一等待。

重要的所有权规则：

- 创建 fd 的对象负责最终 `close`；
- 把 fd 注册到 epoll 不会把所有权转给 epoll；
- `native_handle()` 只是借用值，调用方不能擅自关闭；
- 关闭后数字可能被内核复用，因此不能保存旧 fd 并继续使用；
- `CLOEXEC` 防止将来 `exec` 新程序时意外继承 fd。

本仓对应代码：`SocketCan` 拥有 CAN fd，`EpollReactor` 只拥有自己的 epoll fd。
`EpollReactor` 已使用 `EPOLL_CLOEXEC`；当前 `SocketCan` 尚未请求 `SOCK_CLOEXEC`，在实现
可部署 daemon 前应补齐并测试。把未完成项说清楚，比把通用原则误写成当前能力更重要。

## 4. 本项目需要掌握的 C++

### 4.1 RAII 与移动语义

RAII（Resource Acquisition Is Initialization）的核心不是“构造函数高级用法”，而是把
资源有效期绑定到对象有效期。`SocketCan` 析构时关闭 fd，`PeriodicScheduler` 析构时请求
停止并 join，避免错误路径忘记清理。

fd 不能由两个对象都认为自己拥有，否则会 double close。因此 `SocketCan` 禁止复制，允许
移动（move）：移动后新对象取得 fd，旧对象变成 `-1`。面试时应能解释“move 不是复制
一个 fd 数字，而是转移关闭责任”。

备选的手工 `open/close` 可工作，但每个早退分支都必须正确清理，异常和后续维护更容易泄漏。

### 4.2 mutex、atomic 与数据竞争

数据竞争（data race）是多个线程并发访问同一内存，其中至少一个写入且没有正确同步；
在 C++ 中会导致未定义行为（undefined behavior），不只是“偶尔读到旧值”。

本项目的选择：

- `OutputCommand` 是多个字段组成的一致快照，用 mutex 保护；只把各字段改成 atomic 不能
  自动保证它们属于同一条命令；
- running、stop flag 和诊断计数是独立标量，使用 atomic；
- `memory_order_relaxed` 适合不承担同步职责的统计计数；
- acquire/release 用于发布和观察线程生命周期状态。它们建立必要的可见性顺序，但不代替
  对复合状态的 mutex。

面试追问“为什么不用无锁队列”时，可以回答：当前频率和线程数不需要它，mutex 版本更容易
证明一致性；周期 trace 使用 `try_lock`，拿不到锁时丢诊断而不阻塞监督路径。真正需要无锁
结构前应先测量争用。

### 4.3 显式错误返回与异常边界

POSIX/Linux API 常用返回值和 `errno` 表示失败。本仓用 `Result<T>` 把错误类别和诊断
message 带回调用方，控制路径不依赖异常传播。周期 callback 外层仍有 catch-all，避免异常
越过线程入口触发 `std::terminate`；捕获后记录 worker error 并 fail closed。

这是一个最小实现，不是完整的 `std::expected` 替代品：当前 `Result<T>` 要求 `T` 可默认
构造，错误 message 也可能分配内存。因此不能把它宣传为无分配、硬实时错误通道。

### 4.4 固定宽度类型与线协议（codec 知识卡）

**一句话直觉**：总线上只有字节，没有 C++ 结构体；主机觉得“连续”的字段，另一端可能看到
不同的字节序和空洞。

**解决的工程问题**：把冻结的 CAN V1 合同变成可调用、可单测的 `encode`/`decode`，让
Runtime 与未来模拟器共享同一套线级表示，而不是各自 `memcpy`。

**用户态 / 内核态**：codec 纯用户态，不碰 fd。内核只在之后的 `write(can_socket)` 看到
已经排好的 8 字节。

**本仓调用链**：`rcr::can_v1::{encode_*,decode_*}`（`linux/include/rcr/can_v1.hpp`、
`linux/src/can_v1.cpp`）↔ `CanFrame` ↔（后续）`SocketCan`。

**输入输出与单位**：wire DTO 使用 u8/u16；多字节为大端；`validity_10ms` 单位 10 ms；
绝对 deadline 只存在于进程内，换算函数 `validity_10ms_from_deadline` /
`deadline_from_validity_10ms` 显式完成。

**线程与状态**：codec **无状态**。session 是否当前、序号是否比上次新，属于模拟器/
应用层；无状态 decode 只保证线级合法。

**错误行为**：非法帧返回 `Errc::Rejected` / encode 侧 `InvalidArgument`，不抛异常，
以便将来在 epoll 循环里只计数、不崩进程。

**方案 vs 备选**：选逐字段打包，而不是 `#pragma pack` + `memcpy`。后者在不同 ABI、
对齐和字节序下会静默错位；前者可用 golden vectors 字节级对照。也不引入 Protobuf/
ISO-TP：V1 消息已能装进 8 字节。

**证据**：`test_can_v1` 覆盖 golden vectors、回绕比较、有效期换算、截断/脏帧。

**观察实验**（不是性能证据）：

```bash
./build/linux/tests/test_can_v1
# 对照 protocol/can_v1/golden_vectors.tsv 中同一十六进制串
```

**面试三点**：为什么不能 memcpy？相对有效期解决什么？codec 与业务校验边界在哪？

**不能声称**：codec 通过 ≠ 总线通信已端到端验证；也不证明物理 CAN。

`uint16_t` 表示明确 16 bit，但 C++ 结构体仍可能有 padding、对齐和主机字节序差异。
进程内 `OutputCommand` 使用 64-bit session/sequence 和绝对单调 deadline；CAN V1 为适应
8-byte 帧和未来独立 MCU 时钟，使用 u16 session/sequence 和相对有效期。两者是语义转换，
不是同一个内存类型。

## 5. 时间与调度

### 5.1 `CLOCK_MONOTONIC` 与墙钟

墙钟（`CLOCK_REALTIME`）会因 NTP 或人工校时跳变，不适合 watchdog 和 deadline。
`CLOCK_MONOTONIC` 表示系统启动后的单调时间，适合比较经过时长；它的数值没有跨机器、
跨 MCU 的共同纪元，所以 CAN 线上发送相对有效期，接收端换成本地 deadline。

### 5.2 相对睡眠与绝对睡眠

若每轮执行 `sleep(period)`，callback 执行时间会累积到周期：

```text
实际周期 ≈ sleep 时间 + callback 时间 + 调度延迟
```

本项目先计算绝对目标，再调用
`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline)`。callback 偶尔变慢不会让之后
所有目标永久后移；过载时跳到未来边界，避免补跑旧周期形成追赶风暴。

绝对睡眠只解决累计漂移，不消除调度抖动（jitter）。唤醒仍会受内核、IRQ、其他任务、
电源管理和权限影响。

### 5.3 `SCHED_FIFO` 不等于硬实时

`SCHED_FIFO` 是 Linux/POSIX 实时调度策略：高优先级可运行线程会压过普通线程，自己阻塞、
yield 或被更高优先级线程抢占前可以继续运行。风险是错误的高优先级死循环可能饿死系统。

本项目在线程内部申请策略并记录结果：

- 非强制模式失败后继续普通调度，同时保存 `fifo_error`；
- 强制模式失败则不进入周期循环；
- 没有测量前不说“达到实时”；普通内核即便启用 FIFO 也没有硬实时最坏时延保证。

## 6. Linux I/O 与通信

### 6.1 阻塞、非阻塞和 readiness

阻塞 `read` 在没有数据时让线程睡眠；非阻塞 `read` 立即返回 `EAGAIN/EWOULDBLOCK`。
epoll 报告的是“现在做某类 I/O 可能不会阻塞”（readiness），不是替应用完成读取。

即使 epoll 刚报告可读，也应把 fd 设为非阻塞并处理 `EAGAIN`：多个事件、错误或未来代码
变化都可能让 readiness 与真正读取之间出现竞态窗口。

### 6.2 为什么使用 epoll

`select` 每次等待都重建 fd 集合并受 `FD_SETSIZE` 约束；epoll 在内核保留 interest list，
适合一个线程等待多个长期 fd。V1 数量并不大，选择 epoll 的主要价值是把 CAN、停止唤醒、
信号和定时事件纳入同一个明确生命周期，而不是追求百万连接吞吐。

当前 `SocketCan::receive(timeout)` 使用 select，适合独立阻塞式调用和测试；未来 daemon
使用 `EpollReactor + nonblocking SocketCan`。这两条路径不应在同一个 I/O 线程混用等待。

V1 使用默认 level-triggered 行为：只要数据仍可读就会继续报告。相比 edge-triggered，
它更容易正确实现；当前没有证据需要 ET 的复杂 drain-until-EAGAIN 合同。

### 6.3 eventfd、signalfd、timerfd 各解决什么

这些是 Linux 特有 fd，不是通用 POSIX 接口：

| 机制 | 用途 | 本项目位置 |
|---|---|---|
| eventfd | 线程间发送轻量计数/唤醒，让阻塞 epoll 立即退出 | 阶段 2 daemon 停止路径 |
| signalfd | 把已屏蔽信号变成可读事件，统一处理 SIGINT/SIGTERM | 阶段 2 daemon 生命周期 |
| timerfd | 把定时到期变成 fd readiness | 节点模拟器周期 heartbeat 候选实现 |

不直接在异步 signal handler 中操作 C++ 对象，因为 handler 可安全调用的函数集合很小，
mutex、iostream 和多数对象操作都不安全。signalfd 让正常线程上下文处理关闭顺序。

### 6.4 SocketCAN 与 vcan

SocketCAN 把 CAN 作为 Linux socket：应用创建 `PF_CAN/SOCK_RAW/CAN_RAW` socket，查询接口
index，bind 后用 `read/write` 收发内核 `can_frame`。这样同一套应用 API 可连接 `vcan0`
或将来的物理 `can0`。

vcan 仍经过内核 CAN socket、过滤和 fd 唤醒路径，适合自动化验证进程、帧和 epoll；它不
模拟收发器、电压、终端电阻、仲裁时序、错误计数或 bus-off，因此不能替代物理 CAN 证据。

## 7. Runtime 监督语义

### 7.1 状态机为什么优于散落布尔变量

如果用 `enabled && ready && !fault && !estop` 分散判断，新增恢复路径时容易漏掉组合。状态机
明确列出模式、事件、允许迁移和拒绝原因，可逐条测试。

当前关键规则：只有 `Active && interlock_ready` 接受输出；timeout 或联锁丢失进入 Hold；
Resume 只回 Idle，必须重新 Activate；EStop 锁存并显式 Reset。这里是软件行为演示，不是
硬件安全功能。

### 7.2 watchdog 与 deadline 的区别

- watchdog 回答“命令流是否持续到达”；长时间没有新命令就改变 Runtime 状态；
- 每条命令的 deadline 回答“这一条到达或消费时是否还新鲜”；
- session 防止重启/重新激活后旧会话命令被接受；
- sequence 防止同一会话中的重复、倒退和乱序。

四者解决不同故障，不能只保留一个。恢复后清空 mailbox、session 和 sequence，避免自动
重放最后目标。

### 7.3 latest-wins 什么时候正确

普通输出目标是可覆盖状态，例如“当前 LED 目标为 01”；消费者落后时追赶 00、01、00、01
的历史通常没有价值，因此 mailbox 只留最新值并记录覆盖数。

输入边沿、fault 和状态迁移是事件，丢掉中间项可能改变语义，不能使用 latest-wins；它们在
真实 I/O 生产者出现时进入有界事件队列，溢出必须可见并升级故障。

## 8. 当前与下一阶段关键调用链

### 8.1 发布普通输出

```text
Application
  → LinuxRuntime::publish_output_command
  → 读取 CLOCK_MONOTONIC
  → state mutex 下检查 running/state/session/sequence/deadline
  → CommandMailbox::publish（覆盖旧目标并计数）
  → MonotonicWatchdog::kick
  → TraceBuffer::record
```

状态检查、sequence 提交、publish 和 kick 放在同一锁区间，是为了避免另一个线程刚进入
Hold 后仍有命令穿过门控。

### 8.2 周期监督

```text
PeriodicScheduler worker
  → absolute clock_nanosleep
  → 记录 wakeup lateness
  → LinuxRuntime::on_tick
  → watchdog check
  → 首次超时：Active → Hold，清 mailbox/session，记录 trace
```

worker 异常退出后 Core 通过 `running=false` 关闭发布和消费，但 `mode` 可能仍显示 Active；
未来 daemon 必须观察 worker_error、记录可见 Fault 并非零退出。这是“控制 fail closed”与
“应用生命周期升级”的职责边界。

### 8.3 CAN V1 数据路径（codec 已实现，I/O 集成待做）

```text
内部命令 / 节点状态
  → can_v1::encode_*（范围检查；deadline→validity_10ms）
  → CanFrame
  →（待）SocketCan::send
  → Linux CAN / vcan0
  →（待）SocketCan::receive
  → can_v1::decode / decode_*（ID/flags/DLC/version/reserved）
  → 模拟器业务：session / seq_newer / expiry
  → encode_output_status 原路返回
```

codec 只负责线级表示合法性；“是不是当前 session”“序号是否比上次新”依赖节点状态，属于
模拟器/应用层。当前证据等级：合同与 codec 为“使用过”（`test_can_v1`）；双进程 vcan
路径仍为“理解过/待实现”。

## 9. 高频面试题与项目化回答

### Q1：这个项目是实时系统吗？

回答要点：它是普通 Linux 上具有实时性关注的 Runtime 原型。使用单调时钟、绝对睡眠、
可选 SCHED_FIFO 和延迟统计，但还没有 Orange Pi 压力基线、PREEMPT_RT 对照或最坏时延
证明，所以不声称硬实时。

常见追问：怎样进一步验证？回答平台固定、governor/affinity/权限记录、空载/压力矩阵、
分位数与 miss，再与 PREEMPT_RT 同条件比较。

### Q2：为什么不是每个设备一个线程？

当前只有 CAN、停止、信号和少量定时事件。单 epoll I/O 线程减少线程切换，集中 fd 所有权
和关闭顺序。若以后某个设备处理会阻塞或 CPU 很重，应把重活移出 I/O 线程，但不能先建线程池。

### Q3：为什么 mailbox 用 mutex，不全部用 atomic？

命令由多个相关字段组成，需要一致快照。单字段 atomic 不能保证 session、sequence、deadline
来自同一次发布。mutex 简单可证明，统计计数才用 relaxed atomic。

### Q4：怎样防止重启后旧命令生效？

CAN V1 合同规定 Node 每次启动产生新 session；命令携带当前 session 和单调 sequence，并有
有限有效期。Runtime 离开 Active 时已经会清空 mailbox 和会话状态；Node 侧拒绝
session mismatch、陈旧序号和过期命令仍需由模拟器与进程测试验证，当前只能说“已设计”。

### Q5：为什么不用 protobuf、ISO-TP 或通用 Transport？

V1 四类消息都能放进经典 CAN 8 bytes。显式 codec 更容易审查字节、范围和故障行为；当前只有
SocketCAN 一个真实传输，没有两个行为不同的需求支持通用抽象。长报文或第二种真实传输出现后
再评估，不为“以后可能”增加复杂度。

### Q6：SCHED_FIFO 设置失败怎么办？

线程自己申请策略。非强制模式继续普通调度并记录 errno，强制模式拒绝启动。这样部署权限问题
可见，不会把实际普通调度误报成 FIFO。服务端还需最小化权限，不能长期 root 运行。

### Q7：epoll 返回可读后，read 一定成功吗？

不保证。readiness 与调用 read 之间状态可能变化，错误和关闭事件也可能同时出现。因此 fd 采用
非阻塞模式，循环处理到 EAGAIN，并显式处理 EPOLLERR/EPOLLHUP。当前 daemon 尚未实现，这部分
属于阶段 2 验收项，不能说已经完成。

### Q8：如何证明代码没有线程问题？

不能靠一次测试证明“没有”。当前用 mutex/atomic 建立明确所有权和同步，普通测试与 ASan/UBSan
已运行；TSan 在当前受限环境没有成功启动，因此应在支持环境重跑，并增加重复 start/stop、故障
时序和压力测试。回答时同时说明工具覆盖边界。

## 10. 可重复观察实验

以下命令只读或在构建目录产生文件。工具不存在时记录缺失，不要把安装工具混进功能修改。

### 10.1 看见构建、测试目标与跳过状态

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux -N
ctest --test-dir build/linux -V -R socketcan_vcan
```

重点观察 `Skipped` 与 `Passed` 的区别。阶段验收使用 `--require-vcan`，缺接口必须失败。

### 10.2 看见绝对时间睡眠系统调用

```bash
strace -f -e trace=clock_nanosleep ./build/linux/rcr_benchmark \
  --duration-ms 50 --period-us 10000
```

`strace` 会增加系统调用跟踪开销，只用于理解调用链，不能用其输出做延迟 benchmark。

### 10.3 看见线程调度属性

运行 benchmark 或未来 daemon 时，可从另一终端观察：

```bash
ps -L -o pid,tid,cls,rtprio,pri,psr,comm -p <PID>
chrt -p <TID>
```

没有相应权限时 FIFO 失败是预期分支，应检查程序输出中的 `fifo_enabled` 和 `fifo_error`。

### 10.4 看见 SocketCAN 内核路径

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
ip -details link show vcan0
./build/linux/tests/test_socketcan_vcan --require-vcan
```

若安装了 can-utils，可额外在另一终端运行 `candump vcan0`。这证明软件帧路径，不证明物理层。

### 10.6 看见 codec 与 golden vectors

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j --target test_can_v1
./build/linux/tests/test_can_v1
```

对照 `protocol/can_v1/golden_vectors.tsv`：同一 `data_hex` 必须能 decode 出约定字段，
再 encode 回相同字节。这只证明编解码合同，不证明 SocketCAN 或物理层。

## 11. 后续模块的知识卡完成模板

每个新模块在合并前补齐以下内容：

```text
主题与一句话直觉：
解决的工程问题：
用户态 / 内核态分别做什么：
本仓调用链与关键文件：
输入输出和单位：
线程、时间与同步：
资源所有权和关闭顺序：
主要错误、errno 与 fail-closed 行为：
选择方案 vs 一个合理备选：
单元测试 / 集成测试 / 实机证据：
一个可重复观察实验：
三个面试问题及追问：
不能声称的能力：
```

近期补充顺序与开发阶段一致：CAN codec → 节点模拟器的 timerfd/epoll → daemon 的
eventfd/signalfd → 有界事件队列 → systemd/权限 → benchmark 统计与 Orange Pi 证据。

## 12. 关联文档

- [Linux Runtime 模块原理](LINUX_RUNTIME.md)：当前实现的模块合同与调用链；
- [CAN V1 线级合同](../protocol/can_v1/README.md)：字段、ID、字节序和拒绝行为；
- [系统架构](ARCHITECTURE.md)：分层、线程与长期边界；
- [当前阶段计划](CURRENT_PHASE_PLAN.md)：近期工作包和退出条件；
- [系统规范](../SPEC.md)：V1 总体范围和验收合同。
