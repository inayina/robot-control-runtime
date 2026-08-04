# “五层一横”架构与 A–G 证据路线

状态：Active

规划日期：2026-08-03

当前主线：P3-A0/A1/A2 已完成（release 合同 + systemd 静态资产 + bring-up 模板/共享
矩阵 runner）；下一主线为到货后的 P3-B0；Orange Pi 实机实测尚未开始。

本文给仓库增加两个稳定坐标：

- **五层一横**回答代码职责、依赖方向和学习主题；
- **阶段 A–G**回答当前只解决哪个核心矛盾，以及用什么证据退出阶段。

旧的“阶段 0–10”和“P1–P3”是已经进入提交、报告和文档的历史执行编号，不整体改名。
它们与本文的映射见第 8 节。系统边界仍以 [SPEC.md](../SPEC.md) 和
[AGENTS.md](../AGENTS.md) 为准；近期 Orange Pi 工作包仍以
[P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md) 为准。

## 1. 总体结构

```text
第 5 层  部署与设备       ThinkPad → SSH/release → Orange Pi → systemd
                            │
第 4 层  Daemon 编排       rcrd / RuntimeDaemon / startup / shutdown
                            │ 组合，不重做机制
第 3 层  Linux 机制        scheduler / fd / epoll / SocketCAN / pthread
                            │ typed event / sampled monotonic time
第 2 层  Runtime Core      state / watchdog / mailbox / queue / supervisor / trace
                            │ decoded value
第 1 层  协议与合同        CAN V1 wire contract / codec / golden vectors

横向层   Evidence Plane    test / fault / benchmark / trace / metadata / 知识卡
```

依赖规则：

1. 第 4 层可以组合第 1～3 层；第 3 层不决定业务恢复策略；
2. 第 2 层接收已解码的值和调用方采样的单调时间，不打开 socket、不等待 fd；
3. 第 1 层只定义线上字节及其合法性，不创建线程、不打开 socket、不访问状态机；
4. 第 5 层只部署和监督进程，不把 systemd 行为藏进 Runtime Core；
5. Evidence Plane 可以观察所有层，但诊断失败不能静默改变控制语义。

源码已按职责归位：`src/core/` 保存状态、watchdog、mailbox、有界队列/监督和 trace；
直接使用 `pthread`、`SCHED_FIFO`、`clock_gettime`、`clock_nanosleep` 的 scheduler/time
位于 `src/linux/`；组合 Core 与 scheduler 的 `LinuxRuntime` 实现位于 `src/daemon/`。
公共头文件仍保持扁平的 `<rcr/...>` 路径，归位不改变 API 或运行时行为。

## 2. 第 1 层：协议与合同

当前映射：

```text
protocol/
└── can_v1/
    ├── README.md
    └── golden_vectors.tsv

linux/include/rcr/can_v1.hpp    Linux 侧 wire DTO / codec API
linux/src/can/can_v1.cpp        无状态显式编解码
```

`golden_vectors.tsv` 继续放在 `can_v1/` 内。向量属于具体协议版本，放到 `protocol/`
根目录会让未来多个合同共享一个含义不清的表。`runtime_ipc_v1/` 只保留为未来方向；在
第二个真实进程边界、字段预算和兼容策略出现前不创建空目录或装饰性头文件。

这一层必须能够回答：

- 帧包含哪些字段、宽度、单位和合法范围；
- 多字节整数采用什么字节序，保留位如何处理；
- `session`、`boot_id` 和 `sequence` 由谁生成、何时变化、如何回绕比较；
- 跨设备不共享 `CLOCK_MONOTONIC` 时，deadline 如何用相对有效期表达；
- DLC、帧类型、节点、版本和字段不合法时，是拒绝单帧还是升级故障；
- 版本不一致时如何拒绝，旧版本是否允许兼容。

核心不变量：**C++ 进程内对象不能通过 `memcpy` 变成线协议。** 当前 CAN V1 使用固定宽度
整数、显式大端读写和 golden vectors，已经在 ThinkPad/`vcan` 路径验证；它不是物理 CAN
电气层证据。

## 3. 第 2 层：Runtime Core

目标是让状态、时间新鲜度、并发传递和故障恢复规则保持为可单测的 C++ 逻辑。这里的
“纯 C++”指不直接操作 fd、socket、systemd 或具体网卡；单调时间由外部采样后以纳秒值传入。

| 模块 | 保护的不变量 | 线程/资源 owner | 失败行为 | 主要证据 |
|---|---|---|---|---|
| `RuntimeStateMachine` | 只允许合法迁移；恢复不直接回到 Active | Runtime 状态锁下修改 | 拒绝非法事件；故障路径 fail-closed | `test_state_machine`、fault matrix |
| `MonotonicWatchdog` | 只比较同一单调时钟域；超时只首次触发 | 周期线程检查，状态路径 arm/kick | Hold、清输出；不自动恢复 | `test_watchdog`、command timeout 场景 |
| `CommandMailbox` | 普通输出形成一致快照；latest-wins 可计数 | 发布/消费双方，经 mutex | 覆盖旧目标并计数；不承载边沿/故障 | `test_mailbox`、mailbox overwrite |
| `BoundedInputQueue` | 内存有界；故障/重启边沿不被后值覆盖 | I/O 单生产者、周期单消费者 | 满时拒绝新事件并锁存 Internal fault | `test_runtime_events`、queue overflow |
| `NodeSupervisor` | online、boot/session、根因和恢复条件一致 | 周期线程驱动；快照可并发读取 | CommLoss/NodeFault/overflow 分别锁存 | `test_runtime_events`、vcan restart |
| `TraceBuffer` | 固定容量；诊断竞争不能阻塞监督周期 | Runtime 写，诊断读 | best-effort 丢 trace 并计数，不改控制状态 | `test_trace` |

规划中的 `BoundedEventQueue` 在现有实现中叫 `BoundedInputQueue`。当前队列只服务具体的
CAN 输入事件，保留具体名称比提前泛化更准确；出现第二种行为不同的事件来源后再评审通用名。

当前还有一个边界债务：`NodeSupervisor::on_tick` 直接接收 `LinuxRuntime&`。它没有执行
syscall，但让纯逻辑依赖了组合对象。只有在修改监督接口或出现第二个调用者时，才把所需操作
收窄为明确回调/端口；不为追求图面纯度先增加一套无人使用的抽象。

## 4. 第 3 层：Linux 机制

这一层建立从 C++ 对象到 libc/pthread、system call 和内核对象的可解释路径。

| 机制 | 当前实现与 owner | 关键失败表现 | 当前证据边界 |
|---|---|---|---|
| `PeriodicScheduler` | worker 拥有线程属性；绝对 `CLOCK_MONOTONIC` 睡眠 | FIFO/affinity 权限或配置错误可见；callback 异常停 worker | ThinkPad 1/5/10 ms 12 格；尚无 Orange Pi 与受控 3 ms callback benchmark |
| `OwnedFd` / `eventfd` / `signalfd` | Daemon 拥有 stop/signal fd；移动、不可复制 | 创建/read/write 错误返回；停止写入唤醒 epoll | `test_owned_fd`、SIGTERM 进程验收 |
| `EpollReactor` | reactor 拥有 epoll fd，不拥有被监视 fd | ERR/HUP 和注册错误显式传播 | `test_epoll_reactor`、daemon I/O loop |
| `SocketCan` | `CanIoLoop` 拥有 CAN raw socket | open/bind/read/write 错误可见 | FakeCan、可选 vcan、双进程验收；非物理 CAN |
| `timerfd` | 节点模拟器事件循环拥有；scheduler 不使用它 | 读数/到期错误终止模拟器循环 | node simulator 测试与 vcan 验收 |
| CPU affinity / `SCHED_FIFO` | 周期和 I/O worker 自己申请并回报实际结果 | 请求不等于生效；强制模式启动失败 | ThinkPad 本地证据；普通 Linux，不是 RTOS/硬实时 |

`clock_nanosleep(TIMER_ABSTIME)` 与 `timerfd` 都保留，因为已有两个不同场景：周期 worker
直接等待一个绝对边界；fd 事件循环需要把定时到期并入 epoll。无需再包一层通用 Timer。

观察实验必须与性能基线分开：

```bash
strace -f -e trace=epoll_wait,read,write,clock_nanosleep ./build/linux/rcrd --duration-ms 500
ls -l /proc/<pid>/fd
ps -L -o pid,tid,cls,rtprio,psr,comm -p <pid>
chrt -p <tid>
```

`strace` 会显著扰动时序，只能解释 syscall/阻塞关系，不能把其输出用于 lateness benchmark。
关闭 CAN 接口属于显式故障实验，需要单独的运维步骤和 root 权限，不能在普通单测中偷偷修改
主机网络状态。

## 5. 第 4 层：Daemon 编排

当前映射：`linux/apps/rcrd.cpp`、`RuntimeDaemon`、`CanIoLoop` 和 `LinuxRuntime` 组合对象。
Daemon 只负责把配置、Core 和 Linux 资源连成一个有界生命周期，不重新实现状态机、epoll
或协议合法性。

实际线程模型：

```text
main / application thread
├── startup、wait、故障分类、stop、退出码
├── periodic supervision thread
│   └── watchdog → NodeSupervisor → 状态/故障
├── CAN I/O thread
│   └── epoll(SocketCAN, eventfd, signalfd) → decode → bounded queue
└── optional duration helper thread
    └── 仅 `--duration-ms` 非零时存在；到期后请求停止
```

有界启动顺序：

```text
校验配置/探测 CAN
→ block signal + 创建 signalfd
→ 创建 eventfd、queue、Runtime、Supervisor
→ 启动 periodic worker 并 Boot 到 Idle
→ 打开 SocketCAN/epoll 并启动 I/O worker
→ 可选启动 duration helper
```

任一步失败都逆序回滚已经启动的部分，并映射为稳定退出码。正常关闭顺序是：发布 stop
意图 → eventfd 唤醒并 join I/O → join duration helper → Runtime fail-closed 并 join 周期线程
→ 销毁 I/O/Core/queue → 关闭 eventfd/signalfd。socket 和 epoll 只在 I/O 线程退出后由 RAII
释放。

当前 ThinkPad 已有 SIGTERM、duration、worker failure、重复启停 fd/线程断言，以及显式授权的
`vcan` 接口 down 故障用例（默认 Skip；`sudo ./linux/scripts/run_vcan_iface_down_fault.sh`）。
成体系手工观察记录仍建议按知识库 §10.5 / §10.5.1 做一次对照。

## 6. 第 5 层：部署与设备

```text
ThinkPad
  └── git / SSH / rsync 或板上 checkout
        └── Orange Pi 4 Pro 4GB
              └── systemd（普通服务用户 rcr）
                    └── rcrd → SocketCAN → vcan0
```

已完成的是 P3-A0（release/current）、P3-A1（三个 unit、hardening、`systemd-analyze verify`、
FIFO drop-in 示例）与 P3-A2（`BRINGUP_CHECKLIST.md`、共享 `run_benchmark_matrix.sh`、
`collect_orangepi_host_snapshot.sh`）。尚未完成的是板上原生 aarch64 构建、权限、
journald、冷启动、重启限制、温度/降频观察和 ARM benchmark（P3-B\*）。

部署层 Gate 必须同时记录：板卡/内存/供电/存储、镜像、内核、设备树 model、编译器、CPU
拓扑、governor、温度/降频、service/drop-in、实际 binary SHA-256 和结果枚举。产品页不能
代替这些观察值，ThinkPad 数据也不能代替 Orange Pi 实测。

## 7. 横向层：Evidence Plane

每个工作包在编码同时定义证据，不在项目末尾补一份总结。最小交付如下：

| 项 | 要回答的问题 |
|---|---|
| unit test | 单个不变量和边界值是否成立？ |
| integration test | 跨线程、跨 fd 或跨进程的数据链是否真实经过内核边界？ |
| fault injection | 超时、溢出、退出、重启或非法输入后进入什么状态？ |
| benchmark | 时间相关模块在什么平台、策略、负载、周期和时长下得到什么分布？ |
| trace | 控制状态变化为何发生，诊断丢失是否可见？ |
| environment metadata | 结果属于哪个 commit、机器、内核、权限和工具环境？ |
| interview explanation | 能否用“问题、方案、内核行为、失败表现、证据”五句话讲清？ |

不是每个模块都需要 benchmark；只有存在时间/吞吐目标时才测量。所有重要结论继续区分：

- **理解过**：能解释机制和合理备选；
- **使用过**：仓库代码和测试真实走过该机制；
- **测量过**：在记录完整环境元数据的平台上采集过可复现数据。

当前本地默认 CTest 有 18 个目标；本轮 17 个通过，`test_socketcan_vcan` 因执行环境不能
打开 vcan socket 而 Skip。仓库已有干净提交上的 vcan、19/19 fault matrix 和 ThinkPad
12 格报告；它们仍只属于对应提交和 x86_64 环境。TSan 在当前主机因
`unexpected memory mapping` 记为 `unsupported`，不能写成并发无缺陷。

## 8. 阶段 A–G 与当前状态

| 阶段 | 唯一核心矛盾 | 退出证据 | 当前判断 |
|---|---|---|---|
| A 时间与调度 | 周期为何晚、过载后如何跳过旧边界 | OTHER/FIFO × idle/stress × 1/5/10 ms；分位数/miss；受控 callback 超时；双平台 | **部分**：ThinkPad 12 格已采；缺受控 3 ms callback 和 Orange Pi 对照 |
| B fd 与事件循环 | 一个线程如何等多个事件并有界关闭 | SocketCAN/eventfd/signalfd/timerfd/epoll；重复启停；fd 计数；SIGTERM；接口关闭 | **大部完成**：机制、SIGTERM、fd/线程稳定断言与显式授权的接口 down 用例已落地；正式授权实测记录按需采集 |
| C 并发与背压 | 哪些数据可覆盖、哪些边沿绝不能丢 | latest-wins、bounded queue、overflow fault、trace best-effort、sanitizer/并发测试 | **大部完成**：策略和测试已存在；TSan 仅 unsupported，不能作为通过证据 |
| D 故障与恢复 | 掉线、重启、乱序、旧命令后如何显式恢复 | heartbeat、旧 session、重复/乱序、expired、restart、ack、无自动重放 | **已在 ThinkPad/vcan 使用并验证**；不代表物理总线或安全功能 |
| E 部署 | ARM Linux 服务怎样长期、最小权限、可恢复地运行 | SSH、原生构建、systemd、journal、冷启动、重启限制、ARM benchmark | **部分**：P3-A0/A1/A2 完成；全部板上 Gate（B\*）待做 |
| F 物理总线 | 软件故障模型如何面对真实链路 | physical CAN 或 EtherCAT simple I/O 二选一 | **未开始**；E 关闭后再选，不并行启动两条线 |
| G 窄 ROS 2 Adapter | 上层 API 如何适配而不侵入 Core | 一个命令、一个状态、独立进程/组件、低频接口 | **未开始**；Runtime 生命周期和物理链路稳定后再做 |

阶段 A 有一个硬件依赖冲突：Orange Pi 对照必须等阶段 E 建立可复现部署环境。因此 A 分成
两个 Gate，但仍研究同一个问题：

- **A-T（ThinkPad 基线）**：先于 B；现有 12 格已完成，补受控 callback 超时实验；
- **A-O（Orange Pi 对照）**：在 E 的板上构建和权限 Gate 后执行，完成后才正式关闭 A。

这样不会因板卡尚未形成实测环境而阻塞已完成的 B–D，也不会把 ThinkPad 数据冒充双平台证据。

旧编号映射：

| 旧执行范围 | 本文坐标 |
|---|---|
| 旧阶段 0、P2-W3/W4 | 第 2/3 层 + A/C + Evidence |
| 旧阶段 1 | 第 1/3 层 + B/D |
| 旧阶段 2、P1 | 第 2～4 层 + B/C/D |
| 旧阶段 3、P2 | Evidence Plane + A/C/D |
| 旧阶段 4、P3 | 第 5 层 + E + A-O |
| 旧阶段 5（EtherCAT）或 physical CAN 分支 | 第 1/3/5 层 + F |
| 旧阶段 8 | 第 4 层外侧 Adapter + G |

Modbus、PREEMPT_RT、EtherCAT DC/servo 保留为 Gate 关闭后的独立扩展，不插入 A–G 主线。

## 9. 从当前仓库继续的执行顺序

### 到板前

1. A-T 收尾：`rcr_benchmark --callback-delay-us`（默认 0）已落地；用 1 ms 周期 + 3 ms
   callback 验证 miss 累计与“跳过旧边界、不追赶补跑”，并区分 lateness 与 callback 执行时间；
2. B 收尾：fd/线程稳定断言与显式授权的接口 down 用例已落地；需要证据时执行
   `sudo ./linux/scripts/run_vcan_iface_down_fault.sh`，并保留 `strace`/`/proc`/`ps -L`
   观察步骤（见知识库 §10.5 / §10.5.1）；
3. ~~P3-A1：实现 `rcr-vcan.service`、`rcrd.service` 和默认 disabled 的 simulator unit，完成
   `systemd-analyze verify`；~~
4. ~~P3-A2：冻结 Orange Pi 观察模板和 ThinkPad/ARM 共用 benchmark runner；~~
5. 不为 `runtime_ipc_v1`、ROS 2、物理 CAN 或 EtherCAT 提前增加抽象。
6. 到货后从 `deploy/orangepi/BRINGUP_CHECKLIST.md` 的 B0 开始填写观察值。

### 到板后

1. E-B0/B1：核对实物、镜像、内核、设备树和 CPU 拓扑，板上原生构建并通过功能 Gate；
2. E-B2：普通用户 systemd 生命周期、journal、最小 FIFO 权限、stop/restart/reboot；
3. A-O/E-B3：在同 commit、同周期、同负载与可解释 CPU 条件下采 ARM 矩阵和受控过载；
4. E-B4：冷启动、崩溃限制、新 session、旧命令不重放和 release 回滚；
5. E 关闭后只启动 F 的一条物理链。按当前岗位目标，优先评审 ThinkPad 专用 Intel 网卡
   + SOEM + 简单 I/O SubDevice；没有预算/设备时停在软件链，不同时启动 physical CAN。

### 更后

F 关闭后再做 G：一个低频命令和一个状态接口。ROS 2 只做转换和生命周期适配，不复制
Runtime 状态机、恢复策略或高频周期线程。
