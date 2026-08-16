# Robot Control Runtime SPEC

状态：Draft v0.7
目标平台：ThinkPad 开发机 + Orange Pi 4 Pro 4GB ARM Linux

权威边界：本文定义产品范围、模块合同和不能声称的能力，不负责当前排期。当前 Active
Gate 是 [Closed-Loop Portfolio Freeze](docs/plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)。
Physical Modbus RTU backend 是前置，见
[PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md](docs/plans/PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md)。
最近关闭的是
[Remote Workbench Boundary Gate](docs/plans/REMOTE_WORKBENCH_BOUNDARY_GATE.md)
（`LOOPBACK / NO PHYSICAL PC-ARM`）与
[Modbus I/O Mock / Pre-hardware Gate](docs/plans/MODBUS_IO_MOCK_GATE.md)。原
[V1 发布 Gate](docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md) 的未关闭项保留。用户于 2026-08-13 单独授权
STM32F103 物理 CAN peer 的 SPEC 与实现；该独立实验以
[`firmware/stm32f103/SPEC.md`](firmware/stm32f103/SPEC.md) 为准，不替代作品集收口所需的
实物采集。长期路线不能反向扩大当前 Gate。

## 1. 项目定位

本仓用于补强机器人系统求职能力，不再重复其他仓库已经做过的 FreeRTOS、编码器、
PID、PWM 和单电机控制。核心作品是一个面向 Orange Pi 部署的 Linux Edge Runtime；
Orange Pi 默认 stock 内核缺少 CAN，因此首版把“ARM 构建/安装/调度实测”和“ThinkPad
vcan 功能闭环”分开举证。可选 can1 内核的软件链证据独立记录，不追溯改变 stock 基线。
项目展示以下能力：

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

已经实现的 headless/Qt Device Workbench 是 Runtime 的可选 commissioning/diagnostics
消费者，不是 V1 验收依赖。它只能通过 Application Adapter 读取快照、触发受约束的测试并
写结果；不拥有 Runtime 状态、CAN fd、恢复策略或实时控制周期。

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
- ROS 2 Adapter、Runtime 内的真实 Modbus RTU/现场 I/O、EtherCAT、通用 Transport 抽象；
  Workbench 可另开 commissioning Gate 把已验证的 Orange Pi RTU 接到 Qt，不因此把
  Modbus 写进 `rcrd`；
- Workbench A2 Runtime actuator admission、真实执行器协议、Direct CAN 或 IPC 隔离；
- PREEMPT_RT 内核改造；先建立普通内核基线；
- 训练、仿真环境、数据采集、Nav2 或机械臂算法。

## 3. 硬件选型

### 3.1 已有或已选定

| 硬件 | V1 角色 | 是否必用 |
|---|---|---|
| ThinkPad | 开发、调试、测试、Git/GitHub、对照 benchmark | 是 |
| Orange Pi 4 Pro 4GB（已到手、部分实测） | ARM Linux、SSH、原生构建、systemd 安装、benchmark | 是 |
| Surface Pro 6（Windows） | 可选第二网络对端、外部参考服务端、SSH/远程诊断终端 | 否 |
| ESP32-S3-DevKitC-1-N16R8 | 后续 USB 诊断/故障注入实验 | 否 |
| STM32F103C8T6 Blue Pill + 3.3 V SN65HVD230 + ST-Link + SG90 | 独立物理 CAN 实验；双向协议/PC13、SG90 无负载双位置目视动作和专用仲裁 smoke 已运行，波形与完整故障验收未运行 | 否 |
| Waveshare 普通版 `RS485 CAN HAT`（MCP2515，12 MHz；RS-485 为 UART + SP3485） | CAN 侧：can2 + SPI3/PD23 overlay 已 probe 为 `can0`，并与 STM32F103 完成 dirty-tree 双向 CAN V1、PC13 输出、SG90 无负载双位置目视动作和专用仲裁诊断；RS-485 前置软件链已在 can2 启用 UART7 为 `/dev/ttyS7` 并确认空闲，但尚无物理 RS-485/RTU；不是默认启动或 B4 | 否 |

实物已观察到 aarch64、3.8 GiB 可见内存、6×Cortex-A55 + 2×Cortex-A76、板载以太网、
Wi-Fi、TF 启动盘和 `6.6.98-sun60iw2` 厂商内核。设备树只报告 `sun60iw2`，供电铭牌、
欠压/降频与准确时间同步仍未形成完整证据；产品资料不能替代这些未观察项。

### 3.2 V1 已完成采购与本版停止线

Orange Pi 4 Pro 4GB、启动存储和基础运行配件已经到位。后续独立支线已完成 MCP2515
`can0` probe、STM32F103 双向协议/PC13、SG90 无负载双位置目视动作和专用仲裁 smoke。
这些结果来自 dirty implementation tree，不修改 P1 的 clean vcan/ARM/systemd 证据，也不能
提前关闭当前 V1 发布 Gate、P2 完整硬件验收或 B4。

### 3.3 P1 不再增加的硬件依赖

- 在转接板准确 SKU、控制器、收发器、电平、晶振和 pinout 完成 P2-G0 识别前，不再追加
  CAN/RS-485 接口板；
- 安全继电器、急停、限位、24 V 电源、塔灯和 DIN 导轨器件；
- 新电机驱动器、编码器、电机或电流采样；
- 为“以后扩展”准备的 Modbus、EtherCAT 和通用 I/O 模块。

### 3.4 P2/P3 物理 CAN 阶段的最小清单

物理支线按到货实物重新评审；拥有器件或完成代码构建不代表以下清单已经通过验收：

| 数量 | 类别 | 要求 |
|---:|---|---|
| 1 | Orange Pi SocketCAN 接口 | USB 或 SPI 路径明确；Linux 驱动可验证；逻辑/总线电平与具体模块匹配 |
| 1 | 第二个 active CAN peer | 优先使用驱动明确的 SocketCAN 接口；若选 MCU，才增加匹配的 3.3 V CAN 收发器 |
| 2 | 120 Ω 端接 | 只装在总线两端 |
| 1 批 | 双绞线与端子 | 短距离台架即可 |

第二 CAN 节点在 P2-G5 冻结，只选一个，不同时开发 ESP32 与 F103 两套固件。若实物为
MCP2515 + 5 V 收发器组合板，控制器、收发器、晶振、终端、SPI/INT 电平和 3.3 V 兼容性
必须逐板核实；商品名不能替代原理图和测量。

## 4. 硬件与部署架构图

### 4.1 V1

```text
┌─────────────────────────┐       LAN / SSH       ┌──────────────────────────┐
│ ThinkPad                │ ────────────────────► │ Orange Pi 4 Pro 4GB      │
│ source / test / git     │                       │ ARM Linux                 │
│ vcan functional gate    │ ◄── logs / evidence ─ │ build + systemd install │
└─────────────────────────┘                       └──────────────────────────┘

ThinkPad：vcan/rcrd 功能闭环；Orange Pi stock：无 CAN，验证 ARM/部署/调度
Orange Pi can1：只记录已跑过的 vcan0 + rcrd 软件链，不等于默认服务或 physical can0
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

稳定规划沿用“五层一横”的历史名称；这里表达职责分区，不是严格单向依赖层。详细职责、
证据状态和 A–G Gate 见
[docs/FIVE_LAYERS_ONE_PLANE.md](docs/FIVE_LAYERS_ONE_PLANE.md)。

```text
Protocol Contract       CAN V1 wire format / codec / golden vectors
Runtime Semantics       StateMachine / Watchdog / Mailbox / Queue / Trace / LinuxRuntime
Linux Mechanisms        Scheduler / fd / epoll / SocketCAN / pthread
Process Orchestration   RuntimeDaemon / Device Supervision / startup / shutdown
Deployment              ThinkPad → Orange Pi → systemd
Evidence Plane          test / fault / benchmark / trace / metadata / knowledge cards
Optional Consumers      CLI / Test / headless + Qt Workbench
```

协议区不创建线程、不打开 socket、不访问状态机。Linux 机制不决定恢复策略；
Process Orchestration 可以跨区组合，但不重写协议、epoll 或状态机。Runtime 不依赖 ROS 2、
systemd、Qt、ESP-IDF、STM32 HAL 或具体 CAN 适配板。可选消费者依赖稳定的 Application
Adapter；依赖方向不能反转到 Runtime Core。

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
- 当前库组件已实现并接入 daemon；Orange Pi 已完成原生构建、release/unit 安装与 ARM
  调度矩阵。默认 **stock** 内核 `# CONFIG_CAN is not set`；可选 **can1** 上跑过
  `vcan0 + rcrd` 软件链，但冷启动常驻（B4）和物理 `can0` 仍未关闭。

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

CAN 节点成功应用命令后，同一个接收端本地 deadline 继续作为普通输出 lease。新 Applied
命令刷新 lease；拒绝命令不刷新；到期、软件联锁丢失或节点重启时普通输出归零，恢复后
仍须当前 session 的新序号命令。该合同只证明 simulator/未来普通 I/O endpoint 的软件
fail-neutral 行为，不表示物理执行器、硬件急停、STO 或功能安全已经实现。

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

V1 必须在 Orange Pi 形成 ARM/部署证据，而不是只证明 x86 测试通过：

1. 通过 SSH 初始化普通用户和密钥；禁止在服务中硬编码密码。
2. 记录 OS、内核、架构、编译器和 CPU governor。
3. 在板上原生构建；交叉编译只作为后续优化，不作为首个可复现路径。
4. 安装并核对 systemd unit、依赖、停止门限、重启限制和日志入口；只有运行内核提供
   可用 CAN fd 时，才要求 `rcrd` active。
5. 实时调度权限最小化，只授予所需 capability/limits，不让服务长期以 root 运行。
6. SIGTERM 必须唤醒 epoll、停止周期线程、清空命令并有界退出。
7. 服务实际启动时，重启后必须生成新 session，旧 CAN 命令不能重新生效；stock 无 CAN
   时保留 dependency inactive/unsupported 证据，不制造 FakeCan 服务凑绿。

V1 的网络角色是普通管理 LAN：板载千兆网口与 Wi-Fi 只用于 SSH、依赖安装和证据回传，
尚不作为 EtherCAT 证据。若后续在该板做 SOEM 对照，千兆网口必须独占，管理走 Wi-Fi。

systemd unit 静态资产已落地（旧证据编号 P3-A1，见 `deploy/systemd/`）；release/current
安装与回滚合同已冻结（旧 P3-A0）；bring-up 勾选表与共享 benchmark runner 已落地
（旧 P3-A2，见
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

教学总览：[docs/images/fault-classification-flow.svg](docs/images/fault-classification-flow.svg)
（发现点 → `FaultCode` → Hold/Fault → clear blocker）。下表仍是权威验收合同。

| 场景 | 期望状态/行为 | 恢复 |
|---|---|---|
| 未 Boot 请求 Activate | 拒绝，保持 Disabled | Boot |
| Scheduler 未运行请求 Activate | 拒绝，保持当前状态 | 启动 Scheduler 后再 Activate |
| 联锁未就绪请求 Activate | 拒绝，保持 Idle | 联锁就绪后再次 Activate |
| 命令 deadline 已过 | 拒绝且记录 trace | 新鲜命令 |
| 重复/倒退 sequence | 拒绝 | 更大 sequence |
| Active 中更换 session | 拒绝 | 退出并重新激活 |
| 命令 heartbeat 超时 | Hold，清空 mailbox | Resume → Idle → Activate |
| 已发送命令 ACK 超时 | `FaultCode::AckTimeout`，Hold，清 pending ACK 且不重试 | Resume → Idle → Activate |
| 联锁丢失 | Hold，不再消费命令 | 联锁恢复，Resume，再 Activate |
| 软件 EStop | EStop 锁存 | 联锁恢复 + 显式 Reset；仍需 Activate |
| 节点模拟器退出 | `rcrd` 观察心跳静默后 `FaultCode::CommLoss` 并进入 Fault | 节点重连、自检、新 session、显式恢复 |
| 输入队列 overflow 后叠加其他故障 | `Internal` 可被后续分类覆盖，但 overflow latch 仍阻止 clear | 重启 daemon |
| FIFO 权限不足 | 非强制模式继续并记录错误 | 修正权限或接受普通策略 |
| SIGTERM | `rcrd` 经 signalfd 有界退出、清空输出路径，退出码 0 | 实际服务 active 时按 unit 策略重启 |

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
- ThinkPad 证据基线（旧工作包编号 P2）：证据 schema、ASan+UBSan/TSan 脚本、自动故障矩阵、
  lateness 分位数采样、12 组调度/负载矩阵脚本（`evidence/sanitizer/`、
  `evidence/fault_matrix/`、`evidence/thinkpad_baseline/`）；审计修复后需在干净 commit
  重采，旧目录不是当前 Gate 的通过证据。
- headless Workbench 的 TestRunner、RuntimeApplicationAdapter、CAN Health 和原子
  JSON/CSV ResultWriter；可选 Qt UI 已接同一条链；Actuator 01 仅为 `MOCK / ISOLATED`；
  `rcr_cell_app` 在边缘拥有 CAN 与 CellReadyMapper，ThinkPad Qt `--cell-peer` 经 CEL1
  观察/下发（软件路径；物理闭环证据仍按 Current Gate，缺项 NOT RUN）。
- 独立 STM32F103 裸机 CAN V1 节点、PC13 输出、TIM1/PA8 双位置 PWM 与一次性仲裁诊断固件；
  dirty-tree 双向 CAN、PC13、SG90 目视动作和仲裁结果见 `evidence/stm32f103_can/`，不属于
  Workbench actuator admission。

### 尚未实现 / 未关闭

- Orange Pi：**B4** 冷启动绿灯仍开。B0–B3 是 stock、无 CAN 的本地证据。can1 只证明
  过手动 `vcan0 + rcrd` 软件链，不得写成“daemon 已在 Orange Pi 长期运行”或
  “物理 CAN 已通”。干净 commit 复跑仍开放；
- EtherCAT：G6（干净 commit）与 I/O SubDevice 联调（PDO/OP/WKC）；
- 现场 Modbus 设备、Modbus RTU；physical CAN 的 clean hardware acceptance、PWM 波形、
  断线/bus-off/IWDG 故障矩阵和 `rcrd --can can0`；
- Workbench A2 actuator admission、真实 actuator CAN 合同、实物执行器和进程隔离；
- trace 导出到文件的运维路径；
- ESP32 固件；F103 固件的 clean 同提交重构建/重烧录与完整故障验收。

文档不得把“ThinkPad + vcan 上 daemon/证据可用”或“Orange Pi 安装了 unit”写成
“Orange Pi 上 Runtime 已部署完成”或“硬实时已证明”。

## 14. 阶段关系与排期边界

SPEC 只冻结依赖关系，不维护“今天做到哪一步”：

1. **未关闭的 V1 / P1 发布候选**：收敛同一 clean commit；ThinkPad 关闭 vcan、故障和
   sanitizer 功能证据；Orange Pi 关闭 ARM Release 构建、安装合同和 benchmark 证据。
2. **独立 BSP / Physical CAN 支线**：2026-08-13 经用户显式授权插入并取得 dirty-tree
   `can0`/真实 peer/仲裁/SG90 目视 smoke；它没有按候选计划关闭 clean manifest、波形与完整
   故障 Gate，因此不算 P2 完整关闭，也不改变 V1 顺序。
3. **可选 P3 Runtime 真总线**：只有 Physical CAN 完整 Gate 关闭后才能运行
   `rcrd --can can0`，再验证
   heartbeat、ACK、restart、CommLoss 和 bus-off；先接低风险逻辑状态，不接执行器。
4. **独立扩展 Gate**：Workbench A2、EtherCAT、Modbus、ROS 2 Adapter 和 PREEMPT_RT
   分别评审，不是 V1 依赖，也不能互相借用证据。

当前没有 Active implementation Gate；状态只见
[plans/README](docs/plans/README.md)。最近的
[Remote Workbench Boundary Gate](docs/plans/REMOTE_WORKBENCH_BOUNDARY_GATE.md) 与
[Modbus I/O Mock Gate](docs/plans/MODBUS_IO_MOCK_GATE.md) 已关闭。V1 clean 发布
保留在 [V1 发布 Gate](docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md)；物理 CAN 候选
步骤见 [P2/P3 执行方案](docs/plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md)；EtherCAT、Modbus
等长期候选见 [开发路线参考](docs/plans/DEVELOPMENT_ROADMAP.md)。切换或关闭独立 Gate
必须单独评审，不能由 SPEC 中的章节编号隐式决定。

## 15. V1 / P1 最终验收

- 同一 clean commit 在 x86_64 ThinkPad 与 aarch64 Orange Pi 完成 Release 构建；各平台
  只运行其内核能力支持的测试，`unsupported/not_run` 不能改写成 PASS；
- ThinkPad `vcan0` 上 Runtime 与独立节点模拟器完成双向数据流，并自动化验收 command
  timeout、联锁丢失、乱序、旧 session、ACK timeout 和节点重启；
- Orange Pi 完成 release/current/manifest、普通服务用户、systemd unit 内容、日志入口、
  停止与重启限制验证；stock 无 CAN 时诚实保留 `rcr-vcan` unsupported 与 `rcrd`
  dependency inactive，不要求制造一个假 active 服务；
- ThinkPad 与 Orange Pi 的 Release benchmark 都记录空载/压力、普通/FIFO、权限、CPU、
  governor、周期和时长；空 callback 结果不冒充 CAN/control 端到端时延；
- README、SPEC、当前 Gate、证据摘要和实际行为一致；Workbench 只作为可选消费者列出；
- 不把 can1/vcan、Mock、软件 EStop、普通 Linux benchmark 或安装 unit 描述成 physical
  CAN、真实执行器、功能安全、硬实时或 Orange Pi 默认常驻 Runtime。
