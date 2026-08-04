# Robot Control Runtime SPEC

状态：Draft v0.5  
目标平台：ThinkPad 开发机 + Orange Pi 4 Pro 4GB ARM Linux  
首版原则：除部署主机正常运行配件外不新增通信实验硬件；可运行、可测量、可部署

## 1. 项目定位

本仓用于补强机器人系统求职能力，不再重复其他仓库已经做过的 FreeRTOS、编码器、
PID、PWM 和单电机控制。核心作品是一个可在 Orange Pi 上长期运行的 Linux Edge
Runtime，展示以下能力：

- POSIX 线程、mutex、atomic、单调时钟和绝对周期调度；
- `SCHED_FIFO` 权限处理、降级可观测性和延迟 benchmark；
- `epoll`、SocketCAN、`eventfd`/`signalfd` 式 fd 生命周期管理；
- watchdog、状态机、旧会话/乱序/过期命令拒绝和故障恢复；
- trace、结构化证据、SSH 与 systemd 部署；
- 从 `vcan` 仿真迁移到物理 CAN 时保持明确的软件/硬件边界。

项目是求职 Demo 和研发原型，不声明硬实时、SIL、PL、认证急停或工业安全等级。

## 2. 最小实现决策

### 2.1 V1 必做

```text
CLI / Test
    ↓
Application Layer
    ↓
Linux Runtime Core
    ↓
SocketCAN
    ↓
vcan0
    ↓
CAN Node Simulator
```

同一套程序先在 ThinkPad 开发，再通过 SSH 部署到 Orange Pi 原生编译或运行。
V1 不依赖 MCU、真实 CAN、电机、传感器、继电器、急停按钮或 Dashboard。

### 2.2 为什么这是最小但完整的作品

- `vcan` 仍走 Linux SocketCAN API，可验证 fd、帧、过滤、`epoll` 和错误处理逻辑。
- Orange Pi 提供真实 ARM Linux、权限、服务管理、CPU 调度和资源约束，而不是只在
  笔记本上跑单测。
- 节点模拟器能稳定复现 heartbeat 丢失、乱序、重放、超时和重连，早期比随机接线
  更适合自动化验证。
- MCU 电机环已在其他仓库覆盖，重复实现不会明显增加作品的岗位区分度。

### 2.3 V1 明确不做

- STM32F411、电机、TB6612、编码器、PID、PWM；
- 把 STM32F103 包装成安全 PLC 或认证安全节点；
- ESP32 Wi-Fi、micro-ROS、网页 Dashboard；
- ROS 2 Adapter、Modbus、EtherCAT、通用 Transport 抽象；
- PREEMPT_RT 内核改造；先建立普通内核基线；
- 训练、仿真环境、数据采集、Nav2 或机械臂算法。

## 3. 硬件选型

### 3.1 已有或已选定

| 硬件 | V1 角色 | 是否必用 |
|---|---|---|
| ThinkPad | 开发、调试、测试、Git/GitHub、对照 benchmark | 是 |
| Orange Pi 4 Pro 4GB（已选定、待实测） | ARM Linux、SSH、Runtime、SocketCAN、systemd、benchmark | 是 |
| Surface Pro 6（Windows） | 可选第二网络对端、外部参考服务端、SSH/远程诊断终端 | 否 |
| ESP32-S3-DevKitC-1-N16R8 | 后续 USB 诊断/故障注入实验 | 否 |
| STM32F103C8T6 Blue Pill | 后续裸机/中断/物理 CAN 独立实验 | 否 |

Orange Pi 4 Pro 产品资料给出的预期基线是 Allwinner A733、4GB LPDDR5、板载千兆
以太网、Wi-Fi 6 和 5V/3A Type-C 供电。电源、启动存储和散热属于主机正常运行条件，
不算通信实验硬件。准确板卡版本、镜像、内核、设备树、供电稳定性和接口驱动必须在
P3-B0 由实物观察后冻结，产品资料不能替代部署证据。

### 3.2 V1 新增采购

Orange Pi 4 Pro 4GB 及其可靠电源、启动存储和散热。V1 不新增 CAN、RS-485、EtherCAT
SubDevice、传感器、驱动器或安全器件。

### 3.3 明确暂不采购

- MCP2515、TJA1050、SN65HVD230 或任何 CAN HAT；
- 安全继电器、急停、限位、24 V 电源、塔灯和 DIN 导轨器件；
- 新电机驱动器、编码器、电机或电流采样；
- 为“以后扩展”准备的 Modbus、EtherCAT 和通用 I/O 模块。

### 3.4 可选物理 CAN 阶段的最小 BOM

只有 V1 完成且需要真实总线证据时，才重新评审并采购：

| 数量 | 类别 | 要求 |
|---:|---|---|
| 1 | Orange Pi SocketCAN 接口 | USB 或 SPI 路径明确；Linux 驱动可验证；逻辑/总线电平与具体模块匹配 |
| 1 | MCU CAN 收发器 | 与 ESP32-S3 TWAI 或 F103 bxCAN 配套；3.3 V 逻辑 |
| 2 | 120 Ω 端接 | 只装在总线两端 |
| 1 批 | 双绞线与端子 | 短距离台架即可 |

首个物理 CAN 对端只选 ESP32 或 F103 之一，不同时开发两套固件。ESP32 有现成 USB
调试链路，通常应先选它。MCP2515 + TJA1050 的 5 V 模块不是默认方案：控制器、收发器
和逻辑电平混在廉价模块上，驱动、晶振、终端和 3.3 V 兼容性常需逐板核实。

## 4. 硬件与部署架构图

### 4.1 V1

```text
┌─────────────────────────┐       LAN / SSH       ┌──────────────────────────┐
│ ThinkPad                │ ────────────────────► │ Orange Pi 4 Pro 4GB      │
│ source / test / git     │                       │ ARM Linux                 │
│ benchmark control group │ ◄── logs / evidence ─ │ Runtime + systemd + vcan │
└─────────────────────────┘                       └──────────────────────────┘

ESP32-S3、F103：断开且不影响 V1 验收
```

### 4.2 可选 V1.1

```text
Orange Pi ── USB ── ESP32-S3
                     ├─ heartbeat / status
                     └─ 测试模式 fault injection
```

USB 实验用于学习嵌入式节点协议和恢复，不要求先提取通用 Transport，也不替代 V1 的
SocketCAN 主线。

### 4.3 可选物理 CAN

```text
Orange Pi ── explicit SocketCAN interface ══ 120Ω ══ CAN ══ MCU transceiver ── ESP32 或 F103
                                               两端共两个 120Ω
```

Orange Pi 4 Pro 官方 40-pin 功能列表未声明 CAN，不能预设板载 `can0`。具体 USB-CAN
型号，或 SPI CAN 的控制器、引脚、晶振、中断和设备树，只能在物理 CAN 工作包中冻结。

## 5. 软件架构

稳定规划采用“五层一横”；详细职责、证据状态和 A–G Gate 见
[docs/FIVE_LAYERS_ONE_PLANE.md](docs/FIVE_LAYERS_ONE_PLANE.md)。

```text
第 5 层  Deployment / Device    ThinkPad → Orange Pi → systemd
第 4 层  Daemon Orchestration   rcrd / startup / supervision / shutdown
第 3 层  Linux Mechanisms       Scheduler / fd / epoll / SocketCAN / pthread
第 2 层  Runtime Core           StateMachine / Watchdog / Mailbox / Queue / Supervisor / Trace
第 1 层  Protocol Contract      CAN V1 wire format / codec / golden vectors
横向层   Evidence Plane         test / fault / benchmark / trace / metadata / knowledge cards
```

协议层不创建线程、不打开 socket、不访问状态机。Runtime Core 不依赖 ROS 2、systemd、
ESP-IDF、STM32 HAL 或具体 CAN 适配板；Linux 线程/fd 机制与 Core 规则分层解释。

## 6. 模块合同

### 6.1 `PeriodicScheduler`

- 输入：周期、可选 FIFO 优先级、`require_fifo` 和有界 callback。
- 时钟：统一使用 `CLOCK_MONOTONIC` 纳秒。
- 调度：`clock_nanosleep(TIMER_ABSTIME)`，避免相对睡眠的累计漂移。
- 过载：跳过已经错过的周期，不补跑历史 callback。
- 降级：FIFO 设置失败必须可查询；只有 `require_fifo=true` 才拒绝启动。
- 不负责：CAN I/O、业务算法、日志落盘。

### 6.2 `EpollReactor`

- 拥有 epoll fd，不拥有注册的业务 fd。
- 一个实例最多一个等待线程；业务 fd 关闭前必须先移除。
- 统一承载 SocketCAN、停止唤醒和信号事件，避免每个 fd 各建线程。
- 当前库组件已实现并接入 daemon；systemd unit 静态资产属 P3-A1；bring-up 模板属 P3-A2；
  Orange Pi 实机仍是待办。

### 6.3 `RuntimeStateMachine`

状态：`Disabled`、`Idle`、`Active`、`Hold`、`Fault`、`EStop`。

```text
Disabled --Boot--> Idle --Activate + interlock--> Active
                                           │
                      timeout/interlock/fault/hold
                                           ▼
                                          Hold
                                           │ Resume
                                           ▼
                                          Idle
```

- `Hold → Resume` 只返回 `Idle`；重新输出必须有新的 `Activate`。
- `EStop` 需要软件联锁恢复和显式 `EStopReset`，复位后仍只到 `Idle`。
- `interlock_ready` 与 `EStop` 是软件测试语义，不是功能安全保证。

### 6.4 `CommandMailbox`

- 仅承载可覆盖的普通输出目标，采用单槽 latest-wins。
- 覆盖未消费目标会计入 `drop_count`。
- 输入边沿、fault 和状态事件不得走该邮箱；后续使用有界事件队列，溢出升级为故障。
- mutex 保证整个命令快照一致；原子计数只用于诊断。

### 6.5 `MonotonicWatchdog`

- Active 时 arm，每个已接受的新命令 kick。
- 超时只产生一次 `newly_expired`，Runtime 转入 Hold 并清空输出路径。
- 恢复不会自动重放旧命令。
- Linux watchdog 不等于 MCU watchdog 或硬件安全回路。

### 6.6 `TraceBuffer`

- 固定容量，构造时分配；满后覆盖最旧记录。
- 周期线程使用 `try_lock`，诊断竞争时丢 trace 而不是阻塞监督周期。
- trace 丢失计数可见，但不得改变控制状态。

### 6.7 `SocketCan` 与节点模拟器

- `SocketCan` 负责 Linux CAN raw socket、过滤和非阻塞收发。
- V1 节点模拟器负责 heartbeat、输入状态、输出确认和可重复的故障脚本。
- 模拟器是独立进程，不链接 Runtime 内部状态，避免自测时共享内存掩盖协议错误。
- SocketCAN/FakeCanBus 与独立 `rcr_node_sim` 已实现；模拟器使用 epoll、timerfd、
  signalfd 和非阻塞 SocketCAN，不链接 Runtime 内部状态。

## 7. 普通输出命令合同

Runtime 内部 `OutputCommand` 至少包含：

| 字段 | 规则 | 目的 |
|---|---|---|
| `session_id` | 非零；一次 Active 会话内固定 | 拒绝重启前旧命令 |
| `sequence` | 非零且严格递增 | 拒绝重复与乱序 |
| `deadline_ns` | 非零，`CLOCK_MONOTONIC` 绝对时间 | 拒绝排队后过期命令 |
| `mask` | 非零 | 指定要更新的普通输出位 |
| `values` | 只解释 `mask` 置位部分 | 表达目标状态 |

命令在发布和消费时都检查状态与 deadline。离开 Active 会清空 mailbox、disarm
watchdog，并忘记活动会话。该 C++ 结构不是 CAN 线格式，不得直接按内存布局发送。

## 8. CAN V1 逻辑消息

线级合同已冻结：见 [protocol/can_v1/README.md](protocol/can_v1/README.md)
（`protocol_version = 1`）与 [golden_vectors.tsv](protocol/can_v1/golden_vectors.tsv)。

| 消息 | 方向 | 用途 |
|---|---|---|
| NodeHeartbeat | node → Runtime | 节点存活、boot/session、hb_seq |
| NodeStatus | node → Runtime | 软件联锁、输入快照、fault 摘要 |
| OutputCommand | Runtime → node | session、sequence、相对有效期、8 路演示输出 |
| OutputStatus | node → Runtime | 接受/拒绝结果与输出镜像 |

编解码器与节点模拟器已经实现，并通过同一组 golden vectors 和双进程 vcan 场景验收。
不直接复用 C++ struct，不提前设计跨 CAN/UART/Modbus 的统一消息框架。

V1 Fault Injection 由节点模拟器启动参数控制，不占用正式 CAN 消息，避免生产协议保留
主动制造故障的入口。

## 9. 线程和资源模型

V1 daemon 目标模型：

```text
Application/main thread
    └─ 生命周期、CLI、配置、状态查询

PeriodicScheduler thread
    └─ deadline/watchdog/heartbeat supervision；不得阻塞 I/O

I/O thread
    └─ epoll(SocketCAN, eventfd, signalfd)；解析后投递有界事件
```

- 周期 callback 不写文件、不等待 socket、不动态拼接日志。
- 同一资源只有一个明确 owner；停止顺序先唤醒线程、join，再关闭 fd。
- 文件日志和 trace 导出在非周期上下文执行。
- 将来如无第二个真实 I/O 线程需求，不增加线程池。

## 10. Orange Pi 部署合同

V1 必须在 Orange Pi 完成，而不是只证明 x86 测试通过：

1. 通过 SSH 初始化普通用户和密钥；禁止在服务中硬编码密码。
2. 记录 OS、内核、架构、编译器和 CPU governor。
3. 在板上原生构建；交叉编译只作为后续优化，不作为首个可复现路径。
4. systemd 管理进程启动、停止、重启限制和日志。
5. 实时调度权限最小化，只授予所需 capability/limits，不让服务长期以 root 运行。
6. SIGTERM 必须唤醒 epoll、停止周期线程、清空命令并有界退出。
7. 服务重启后生成新 session，旧 CAN 命令不能重新生效。

P3 的网络角色是普通管理 LAN：板载千兆网口与 Wi-Fi 只用于 SSH、依赖安装和证据回传，
尚不作为 EtherCAT 证据。若后续在该板做 SOEM 对照，千兆网口必须独占，管理走 Wi-Fi。

systemd unit 静态资产已落地（P3-A1，见 `deploy/systemd/`）；release/current 安装与回滚
合同已冻结（P3-A0）；到货前 bring-up 勾选表与共享 benchmark runner 已落地（P3-A2，见
`deploy/orangepi/BRINGUP_CHECKLIST.md` 与 `linux/scripts/run_benchmark_matrix.sh`）。
权威路径说明见 [docs/ORANGE_PI_BRINGUP.md](docs/ORANGE_PI_BRINGUP.md)。
`rcrd` 进程合同见 [docs/RCRD_CONTRACT.md](docs/RCRD_CONTRACT.md)。ThinkPad 上的
`systemd-analyze verify` 与本机 enable 不能写成 Orange Pi 实机证据；勾选表默认
`NOT_RUN` 不等于板上已测。

## 11. Benchmark 合同

每份可比较证据必须记录：

- 设备、日期、OS/内核、编译类型、CPU governor、温度；
- 周期、调度策略、优先级、CPU affinity、是否成功获得 FIFO；
- 空载或 `stress-ng` 负载、测试时长；
- min/max/mean、分位数、deadline miss 和 worker error；
- 相同条件下 ThinkPad 与 Orange Pi 的差异。

顺序：普通策略空载 → 普通策略压力 → FIFO 空载 → FIFO 压力。普通内核结果建立前
不安装 PREEMPT_RT。空 callback 只证明调度唤醒基线，不代表端到端 CAN 时延。

## 12. 故障与恢复验收矩阵

| 场景 | 期望状态/行为 | 恢复 |
|---|---|---|
| 未 Boot 请求 Activate | 拒绝，保持 Disabled | Boot |
| Scheduler 未运行请求 Activate | 拒绝，保持当前状态 | 启动 Scheduler 后再 Activate |
| 联锁未就绪请求 Activate | 拒绝，保持 Idle | 联锁就绪后再次 Activate |
| 命令 deadline 已过 | 拒绝且记录 trace | 新鲜命令 |
| 重复/倒退 sequence | 拒绝 | 更大 sequence |
| Active 中更换 session | 拒绝 | 退出并重新激活 |
| 命令 heartbeat 超时 | Hold，清空 mailbox | Resume → Idle → Activate |
| 联锁丢失 | Hold，不再消费命令 | 联锁恢复，Resume，再 Activate |
| 软件 EStop | EStop 锁存 | 联锁恢复 + 显式 Reset；仍需 Activate |
| 节点模拟器退出 | `rcrd` 观察心跳静默后 `FaultCode::CommLoss` 并进入 Fault | 节点重连、自检、新 session、显式恢复 |
| FIFO 权限不足 | 非强制模式继续并记录错误 | 修正权限或接受普通策略 |
| SIGTERM | `rcrd` 经 signalfd 有界退出、清空输出路径，退出码 0 | systemd 按策略重启（P3） |

## 13. 当前仓库能力

### 已实现并有本地单测

- Scheduler、StateMachine、Mailbox、Watchdog、Trace；
- `epoll` reactor；
- SocketCAN、FakeCanBus、vcan 辅助；
- CAN V1 显式 encode/decode、golden vectors 和独立节点模拟器；
- vcan 双进程场景验收及环境元数据；
- Runtime 组合、命令 session/sequence/deadline 约束；
- 周期 benchmark 程序；
- `rcrd` composition root：OwnedFd、eventfd/signalfd、有界输入队列、NodeSupervisor、
  CAN I/O 线程、有界 SIGTERM/duration 退出（ThinkPad + `vcan0` 证据见
  `evidence/rcrd_acceptance/`）；
- ThinkPad 证据基线（P2）：证据 schema、ASan+UBSan/TSan 脚本、自动故障矩阵、
  lateness 分位数采样、12 组调度/负载矩阵脚本（`evidence/sanitizer/`、
  `evidence/fault_matrix/`、`evidence/thinkpad_baseline/`）；审计修复后需在干净 commit
  重采，旧目录不是当前 Gate 的通过证据。

### 尚未实现

- Orange Pi 实机 systemd 生命周期与 ARM 实测（P3-B）；unit 静态资产与本机自测属 P3-A1；
  release/current 安装与回滚脚本已在 P3-A0 冻结；bring-up 勾选表与共享矩阵 runner 已在
  P3-A2 落地（模板 ≠ 板上 PASS）；
- 压力格在安装 `stress-ng` 之前只能记 `unsupported`；
- trace 导出到文件的运维路径；
- ESP32/F103 固件和物理 CAN。

文档不得把“ThinkPad + vcan 上 daemon/证据可用”写成“Orange Pi 已部署”或“硬实时已证明”。

## 14. 实施路线与退出条件

详细的阶段依赖、当前状态及 EtherCAT/Modbus 学习路径见
[后续开发路线](docs/DEVELOPMENT_ROADMAP.md)。本节保留系统级退出条件摘要。

### 阶段 0：Linux Core

完成 scheduler、状态机、watchdog、mailbox、trace 和单测。退出条件：ThinkPad 测试
和 sanitizer 通过；FIFO 失败可观测；关键并发路径有明确说明。

### 阶段 1：CAN V1 与节点模拟器

完成 CAN 编解码、golden vectors 和独立模拟器。退出条件：自动化覆盖正常、丢
heartbeat、乱序、过期和节点重启；进程间只通过 SocketCAN 通信。

### 阶段 2：可部署 Runtime daemon

完成 daemon、epoll、signalfd/eventfd 和有界退出。退出条件：SIGTERM、worker 异常、
节点离线和重复启动/停止均可自动验证，无 fd 或线程生命周期泄漏。

### 阶段 3：ThinkPad 证据基线

完成 sanitizer、故障矩阵和普通/FIFO、空载/压力 benchmark。退出条件：报告包含环境
元数据与分位数，失败场景可重复。

### 阶段 4：Orange Pi 部署

完成 SSH 文档、systemd、权限和 benchmark。退出条件：冷启动后服务自动运行；压力下
证据可复现；失败不出现无限重启或残留旧会话。

### 阶段 5：EtherCAT I/O SubDevice 实验

ThinkPad P14s Gen 6 的板载 Intel `e1000e` 有线 NIC 专用于 EtherCAT，管理和互联网连接
走 Wi-Fi。先用 SOEM 和一个资料完整的简单 I/O SubDevice，完成扫描、
INIT/PREOP/SAFEOP/OP、PDO、SDO、working counter、掉线、恢复与周期证据。不从
servo drive 开始，也不把普通 Linux 实测结果宣称为工业实时保证。Orange Pi 4 Pro 有
板载千兆网口，但首轮不因此增加第二个主站变量；ThinkPad 基线关闭后，才决定是否在
板上重复同一 SOEM 功能与周期矩阵，且两套证据分开解释。

### 阶段 6：Modbus TCP 实验

在 Runtime V1 和 Orange Pi 部署完成后，以 Orange Pi client、ThinkPad reference server
组成零采购双机实验。只实现 `0x03`、`0x06`、`0x10` 和 exception，验证 MBAP、半包、
transaction、超时与重连，并和 libmodbus 互操作。实验未通过前不接入 Runtime Core。

### 阶段 7：可选实物通信

ESP32 USB、physical CAN 和 Modbus RTU/RS-485 只优先选择一条。RTU 先用 PTY 验证
address、CRC16、帧和 timeout；physical CAN 验收 `can0`、端接、波形、错误计数、
断线和恢复。不为三者提前设计通用 Transport。

### 阶段 8：ROS 2 Adapter 与只读运维接口

只适配已经稳定的 Runtime API，不把 ROS/Web 线程、状态恢复或依赖带入 Core。

### 阶段 9：PREEMPT_RT 与 EtherCAT 周期对照

普通内核基线完成后，Runtime 的内核对照在 Orange Pi 上进行，EtherCAT 的内核对照在
ThinkPad 或后续明确选定的有线主站上分别进行。两组证据不得混为一个平台结论；没有
可测收益则不采用 PREEMPT_RT。

### 阶段 10：EtherCAT DC / servo drive 进阶

只有 DC-capable SubDevice、servo 硬件、风险评审和明确测量目标时开始。I/O 从站基础
实验已经能够证明 Linux 主站能力，不为了展示电机而强行进入高风险阶段。

## 15. V1 最终验收

- 同一 commit 可在 x86_64 ThinkPad 与 aarch64 Orange Pi 构建并通过测试；
- Orange Pi 通过 systemd 运行 daemon，SSH 可查看状态和结构化日志；
- `vcan0` 上 Runtime 与独立节点模拟器完成双向数据流；
- command timeout、联锁丢失、乱序、旧 session 和节点重启均自动化验收；
- benchmark 有空载/压力、普通/FIFO 四组可比较证据；
- README、SPEC、代码注释和实际行为一致；
- 不把软件演示描述成电机控制、硬实时或功能安全系统。
