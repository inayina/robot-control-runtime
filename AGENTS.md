# 工程原则

- 不要为了展示技术而过度设计。优先选择能可靠回答工程问题的最小方案。
- 本仓以补强机器人底层系统求职能力为目的，重点是 Linux 调度、I/O、状态监督、
  可观测性和部署，不重复其他仓库已经完成的电机 PID、编码器、PWM 或 MCU RTOS。
- 如果方案出现职责重复、复杂度过高或偏离真实机器人开发流程，应主动提出修改，
  说明原因、收益和取舍，不机械执行。
- 每个阶段先做出可运行、可测量、可复现的端到端路径，再增加硬件或抽象。
- 普通 Linux、实时 Linux和 MCU RTOS 必须准确区分；未测量时不得声称硬实时。

# 信息与计划 authority

- `AGENTS.md` 只保存跨阶段工程约束，不复制易变化的测试数量、阶段进度或下一周任务。
- 仓库范围、已完成能力和硬件边界以 `SPEC.md` 为准；系统组件关系以
  `docs/ARCHITECTURE.md` 为准；代码职责以 `docs/CODE_OWNERSHIP_MAP.md` 为准。
- `rcrd` 参数、退出码、线程与关闭合同以 `docs/RCRD_CONTRACT.md` 为准；CAN 线级合同以
  `protocol/can_v1/` 为准。
- 任何时候最多只有一份 Current Gate。当前 Active Gate 是
 `docs/plans/PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md`（Physical Modbus RTU → Qt
 Workbench；Qt 在 ThinkPad，RTU 主站在 Orange Pi `/dev/ttyS7`）。最近关闭记录是
 `docs/plans/REMOTE_WORKBENCH_BOUNDARY_GATE.md`（`LOOPBACK / NO PHYSICAL PC-ARM`）
 与 `docs/plans/MODBUS_IO_MOCK_GATE.md`。
 `docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md` 与
 `docs/plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md` 是未关闭的候选 Gate，
 `docs/plans/DEVELOPMENT_ROADMAP.md` 与 `docs/plans/PC_ARM_DEVICE_CONVERGENCE_PLAN.md`
 只给长期参考，不得覆盖 Current Gate。
- Workbench 总体边界以 `docs/workbench/README.md` 为准，局部退出条件读
  `docs/workbench/GATES.md`；Orange Pi 操作入口是 `docs/ORANGE_PI_BRINGUP.md`；证据分类
  读 `evidence/README.md` 与 `docs/EVIDENCE_SCHEMA.md`。
- `docs/archive/` 和 `docs/workbench/archive/` 只解释历史，不发布当前状态。README 可以做
  稳定摘要和链接，但不得复制 Gate 的阶段表或 evidence 的易变结论。

# 当前权威范围

## ThinkPad

- 用于开发、代码审查、单元测试、Benchmark 对照和 GitHub 工作流。
- 可用 `vcan` 完成全部 V1 开发，不承担最终边缘部署职责。
- 当前 P14s Gen 6 的板载 Intel `e1000e` 有线接口可在后续 EtherCAT 实验中独占使用；
  管理和互联网连接走 Wi-Fi。EtherCAT 实验不改变 Orange Pi 的 Runtime 部署职责。

## Orange Pi 4 Pro 4GB

- 已选定为 V1 的 ARM Linux 部署目标；板上已有 **部分实测证据**（SSH、原生构建、
  release/systemd 安装、ARM 调度矩阵；见 `evidence/orangepi*`）。
- 用于 SSH、原生编译、systemd、日志、CPU affinity、调度权限和压力下延迟测试。
- 默认 **stock** 镜像 `# CONFIG_CAN is not set`：无 `vcan`/`can0`，`rcrd` **未**以
  服务形式常驻。可选 **can1** 内核上已跑过 `vcan0 + rcrd` 软件链，不是默认启动，
  不是物理 `can0`，不能把安装合同或 can1 手工验证写成 B4 已关。V1 软件功能链仍以
  ThinkPad `vcan` 为正式对照。
- 可选 **can2** 内核已用 SPI3/PD23 overlay probe MCP2515 `can0`，并与 STM32F103 完成
  dirty-tree 双向协议、PC13、无负载 SG90 双位置目视动作和专用仲裁 smoke。它不是默认
  启动，不是 clean hardware acceptance，也没有关闭 B4、`rcrd --can can0` 或物理故障矩阵。
- 它运行的是 Linux；`SCHED_FIFO` 是 POSIX 实时调度策略，不等于 RTOS 或硬实时保证。
- 规格须以 P3-B0 实物观察为准（已记录 hostname/`sun60iw2`/3.8Gi 可见内存/大小核等）；
  产品页不能替代证据。
- 官方 40-pin 功能列表未声明板载 CAN；当前 `can0` 来自已确认的 Waveshare 普通版
  `RS485 CAN HAT`、MCP2515、SPI3 和单独 overlay，不能反向写成 SoC 原生 CAN 或通用
  树莓派 overlay 兼容。该板的 CAN 侧已有上述实测；can2 已独立启用 UART7 为
  `/dev/ttyS7` 并确认无 console/getty/进程占用，但这只关闭串口软件前置项，不是物理
  RS-485 或 Modbus RTU 证据。
- 首轮 EtherCAT 仍使用 ThinkPad 的 Intel 网卡建立 x86 基线。ThinkPad Gate/从站联调
  关闭后，才考虑板载千兆口做 ARM/SOEM 对照；届时管理流量走 Wi-Fi，不得与 EtherCAT
  混写为同一平台证据。

## Surface Pro 6

- 保留现有 Windows，不要求安装 Linux；可选用作第二台普通网络对端、外部参考服务端、
  SSH 客户端或远程诊断终端。
- 没有原生 RJ45，不替代带板载有线网卡的 ThinkPad 承担 EtherCAT 主站 benchmark。
- 不是 V1 必需设备，不为使用它增加 Windows/WSL 构建、部署或维护矩阵。

## ESP32-S3-DevKitC-1-N16R8

- 已有硬件，但不是 V1 依赖。
- 可在 V1.1 作为 USB 连接的诊断/故障注入实验节点，验证嵌入式 heartbeat、序号、
  超时和重连逻辑。
- 不进入 Linux Runtime 控制决策，不为使用现有硬件而强行增加固件工作量。

## STM32F103C8T6 Blue Pill

- 已作为用户单独授权的裸机 bxCAN/中断/物理 CAN 学习实验实现；仍不进入 V1 Runtime
  主线架构，也不由 Linux CMake 构建。
- 当前只支持 CAN V1 heartbeat/status、普通输出 lease、PC13、PA8/TIM1 两档 SG90 PWM 和
  一次性仲裁诊断；不得扩写成通用执行器控制器或 Qt/Runtime physical actuator admission。
- 不再把它描述为认证安全控制器；普通开发板和自研固件不能据此声称功能安全。

## STM32F411、TB6612 和 N20

- 不属于本仓当前架构。本仓不重复已有仓库中的 FreeRTOS、Encoder、PID、PWM 和
  单电机闭环。

# 当前软件架构与 ownership

```text
CLI / Test ───────────────────────────────┐
                                         v
                             RuntimeDaemon (composition root)
                               ├─ LinuxRuntime
                               │   ├─ PeriodicScheduler
                               │   ├─ StateMachine / Watchdog
                               │   └─ Mailbox / ACK / Trace
                               ├─ NodeSupervisor
                               └─ CanIoLoop
                                   └─ EpollReactor + SocketCAN
                                                │
                                                v
                                      vcan0 / future can0
                                                │
                                                v
                                       CAN Node Simulator

Runtime public capability
        ↓
    rcr_workbench
        ↓
Qt Device Workbench (optional, default OFF)
```

- `linux/src/core` 保存不依赖 Linux I/O 的可测试 building blocks；`linux/src/runtime` 的
  `LinuxRuntime` 拥有活动 state/watchdog/mailbox/trace/ACK 实例与原子 Runtime 事务。
- `linux/src/linux` 拥有 fd RAII、scheduler primitive、epoll、eventfd/signalfd、SocketCAN 和
  `CanIoLoop`；它不决定 Runtime 恢复策略。
- `linux/src/supervision/NodeSupervisor` 解释 heartbeat/session/restart/CommLoss 并决定故障
  升级；`linux/src/daemon/RuntimeDaemon` 只做组件装配、worker 生命周期、关闭顺序和汇总。
- `EpollReactor` 已通过 `CanIoLoop` 接入真实 fd 数据流；不得再建立空转 I/O 线程或第二套
  CAN fd ownership。
- CAN V1 直接使用 SocketCAN，不预先设计通用 Transport。`NodeSupervisor` 继续依赖具体
  `LinuxRuntime`，直到出现第二个真实 Runtime/caller 才评审接口。
- ROS 2 Adapter、Dashboard、Modbus、EtherCAT、PREEMPT_RT 和 MCU 固件均不属于 V1。
- 状态机中的联锁和 EStop 是软件行为演示，不是硬件安全功能。
- Dashboard 将来仍然只读；ROS 2 Adapter 只做 Topic/API 适配，不侵入 Runtime Core。

## Workbench 边界

- 固定依赖方向为 `rcr` → `rcr_workbench` → optional Qt；
  `RCR_BUILD_QT_DEVICE_WORKBENCH=OFF` 必须保持默认。
- `linux/src/workbench/application` 只做 Runtime adapter/DTO；`services` 承载可复用的 headless
  diagnostics workflow；`profile` 只保存隔离配置和 `MOCK / ISOLATED` actuator profile。
- Qt controller/UI 只拥有 QObject、signal/slot、widgets 和 presentation state；不得打开
  SocketCAN、拥有 `RuntimeDaemon` 状态、复制 supervision/health 判定或把 Qt timer 当控制周期。
- 当前 Qt 与 Runtime 同进程，不能声称 UI crash 与 Runtime crash 已隔离。Actuator Mock
  不发送 motion CAN 帧，不得写成实物执行器闭环。

# 仓库与构建边界

- 顶层 `linux/`、`protocol/`、`deploy/`、`experiments/`、`evidence/`、`docs/` 的职责已经
  稳定；不得为了套通用模板搬成根 `src/`，也不得混合协议、部署、实验、证据和文档。
- `linux/` 是独立 CMake 工程。`rcr` 保持 Qt-free；`rcr_workbench` 是非 Qt 可复用消费者层；
  Qt6 UI 只在显式开启选项时构建。`linux/CMakeLists.txt` 是 source/target 权威清单。
- public headers 保持在 `linux/include/rcr/`；没有真实兼容收益时，不批量移动 include path、
  namespace 或 public API。
- `protocol/` 只保存已冻结的 CAN 线级合同、codec 对照和 golden vector；不把 Linux C++
  类型直接共享给 MCU，也不并入 `linux/`。
- `experiments/` 中的 Modbus、EtherCAT、multibus 和 realtime 实验使用各自构建/验证路径，
  不得因已有 demo 就升级为 Runtime production feature。
- `deploy/` 管 systemd、release、Orange Pi bring-up/recovery；`linux/scripts/` 管 Runtime
  开发、测试和 verification 辅助。引用密集的恢复工具不为目录美观而迁移。
- `evidence/` 与 `docs/` 分离。历史 evidence 路径不轻易重命名；目录名或阶段号不决定
  PASS，必须读对应 README/manifest 与环境元数据。
- `firmware/` 当前只记录可选实验边界，不由 Linux CMake 递归构建。
- Orange Pi 部署与 ESP32/STM32 烧录使用各自原生流程，不建立统一超级构建。

# 计划与实施顺序

- 不在本文件维护固定的 1–N 路线；实施顺序只由唯一 Current Gate 决定。
- 新工作开始前先确认它属于 Runtime、Workbench、部署还是独立 experiment，再读取对应
  authority 和退出条件。Roadmap 上存在不等于已经授权实施。
- 当前 Gate 关闭后，physical CAN、EtherCAT、Modbus、PREEMPT_RT 或 ROS 2 Adapter 必须
  重新比较证据价值、硬件条件、回滚成本和职责边界，只选择一个新的主要 Gate。
- EtherCAT 若被选择，首轮仍在 ThinkPad 独占 Intel 有线网口上做 SOEM + simple I/O
  SubDevice，不从 servo 开始；ARM 对照必须等 x86 Gate 关闭。
- 可选实物通信一次只推进一条；没有准确 SKU/pinout/驱动/回滚条件时不得上电或改设备树。

# 实现决策约束

- 新抽象至少需要两个已经存在、行为不同的真实实现；“以后可能支持”不算使用场景。
- latest-wins 邮箱只承载可覆盖的普通输出目标。输入边沿、故障和状态迁移不得静默覆盖。
- 所有命令必须定义会话、单调序号和 `CLOCK_MONOTONIC` 截止时间；恢复后不得自动
  重放旧命令。
- Benchmark 必须记录内核、调度策略、权限、CPU governor、负载、周期和时长。
- Fault Injection 默认关闭，并与正常命令入口分离；仿真结果不能冒充实物结果。
- 没有物理安全回路时，禁止用“安全”“急停已保证”等措辞描述软件状态机。

# 模块说明与中文注释

- 每实现一个模块，必须在开始编码前向用户说明问题、工程约束、所属层、职责边界，
  以及为什么该职责不放在其他模块。
- 编码前至少比较当前选择与一个合理备选方案，说明不选其他方案的具体原因，例如
  复杂度、实时性、故障行为、依赖、可测试性、部署成本或当前没有真实需求；不能只说
  “这样更好”或用技术偏好代替工程依据。
- 说明至少覆盖输入输出、数据流、时间/线程模型、资源所有权、失败行为和验证方法。
- 编码完成后必须用面向当前项目的语言解释代码怎样工作，包括关键调用链、状态变化、
  并发或时间假设；不能只罗列类名、函数名或修改文件。
- 如果实现中发现原方案不成立，应先说明证据和调整理由，再改变设计；不得为了保持
  最初计划而保留已经没有必要的抽象或硬件职责。
- C/C++ 与固件代码应给关键设计意图添加中文注释，特别是并发、时钟/单位、状态迁移、
  协议编码、权限降级和故障恢复。
- 注释解释“为什么”和不可见约束，不逐行翻译代码。
- 现有模块被修改时，在同一变更中补齐相关原理说明和关键注释；不为注释而重写
  未触及模块。
- 实现结束必须报告测试或实物证据，说明实际行为是否符合设计，并明确替代方案、已知
  取舍和仍未实现的能力。
- 普通私有辅助函数、机械重命名和格式调整不需要逐项方案评审；解释深度应与职责、
  风险和不可逆性相匹配，避免为了展示分析而打断实现节奏。

# 学习与面试知识库

- 默认用户只大致了解 C/C++、Linux 内核、操作系统通信和实时系统。设计与讲解不能假设
  用户已经熟悉 syscall、fd、线程调度、内存模型、协议编码或 systemd。
- `docs/KNOWLEDGE_BASE.md` 是持续维护的学习入口。每次新增或实质修改模块，必须在同一
  变更中补充相关知识卡；未形成可讲述、可验证的知识材料，不算完成阶段退出条件。
- 每张知识卡至少回答：它解决什么问题、用户态与内核各做什么、数据/调用链怎样流动、
  时间和线程模型、资源由谁拥有、失败时发生什么、为什么不选合理备选、如何动手验证。
- 首次出现的术语要给出中文解释和常用英文名。讲解从直觉模型开始，再落到本仓代码；
  不能只复制 API 手册，也不能用更多未解释术语解释一个术语。
- C++ 内容要解释与本项目有关的对象生命周期、RAII、移动语义、mutex/atomic、内存序、
  固定宽度类型、错误传播和未定义行为风险；不为覆盖语法大全而扩写无关教程。
- Linux 内容要解释进程/线程与内核边界、syscall、fd、阻塞/非阻塞、epoll、时钟、调度、
  信号、SocketCAN、权限和关闭顺序；明确哪些是 POSIX，哪些是 Linux 特有机制。
- 知识库中的面试题必须给出基于本项目证据的回答、常见追问、边界和不能声称的能力。
  明确区分“理解过”“在代码中使用过”“在目标硬件测量过”，不得背诵式夸大。
- 每个重要主题至少提供一个低风险、可重复的观察命令或测试；说明工具本身是否会扰动
  时序，以及缺少权限、内核能力或硬件时怎样解释结果。
- 源码注释仍只解释设计意图和不可见约束。基础教程、面试问答和长篇原理放知识库，
  避免逐行注释降低代码可读性。
