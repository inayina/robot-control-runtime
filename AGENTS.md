# 工程原则

- 不要为了展示技术而过度设计。优先选择能可靠回答工程问题的最小方案。
- 本仓以补强机器人底层系统求职能力为目的，重点是 Linux 调度、I/O、状态监督、
  可观测性和部署，不重复其他仓库已经完成的电机 PID、编码器、PWM 或 MCU RTOS。
- 如果方案出现职责重复、复杂度过高或偏离真实机器人开发流程，应主动提出修改，
  说明原因、收益和取舍，不机械执行。
- 每个阶段先做出可运行、可测量、可复现的端到端路径，再增加硬件或抽象。
- 普通 Linux、实时 Linux和 MCU RTOS 必须准确区分；未测量时不得声称硬实时。

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
- 它运行的是 Linux；`SCHED_FIFO` 是 POSIX 实时调度策略，不等于 RTOS 或硬实时保证。
- 规格须以 P3-B0 实物观察为准（已记录 hostname/`sun60iw2`/3.8Gi 可见内存/大小核等）；
  产品页不能替代证据。
- 官方 40-pin 功能列表未声明 CAN，不能预设板载 `can0`；物理 CAN 阶段单独选择有明确
  Linux 驱动的 USB-CAN，或把 SPI CAN 作为独立驱动/设备树实验。
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

- 当前停放，不进入主线架构。
- 仅在以后出现明确的裸机、中断或物理 CAN 学习目标时建立独立实验。
- 不再把它描述为认证安全控制器；普通开发板和自研固件不能据此声称功能安全。

## STM32F411、TB6612 和 N20

- 不属于本仓当前架构。本仓不重复已有仓库中的 FreeRTOS、Encoder、PID、PWM 和
  单电机闭环。

# V1 软件架构

```text
CLI / Test
    ↓
Application Layer
    ↓
Linux Runtime Core
    ├── PeriodicScheduler
    ├── RuntimeStateMachine
    ├── CommandMailbox
    ├── MonotonicWatchdog
    └── TraceBuffer
    ↓
SocketCAN + EpollReactor
    ↓
vcan0
    ↓
CAN Node Simulator
```

- 第一版直接实现 SocketCAN，不预先设计通用 Transport。
- `EpollReactor` 只有在接入真实 fd 数据流时才进入 Runtime 线程，不建立空转 I/O 线程。
- ROS 2 Adapter、Dashboard、Modbus、EtherCAT、PREEMPT_RT 和 MCU 固件均不属于 V1。
- 状态机中的联锁和 EStop 是软件行为演示，不是硬件安全功能。
- Dashboard 将来仍然只读；ROS 2 Adapter 只做 Topic/API 适配，不侵入 Runtime Core。

# 仓库与构建边界

- `linux/` 是独立 CMake 工程，只包含 C++、POSIX、Linux fd 和 SocketCAN 代码。
- `protocol/` 只保存已冻结的 CAN 线级合同。没有 encode/decode 和 golden vector 前，
  不创建装饰性的共享协议头。
- `firmware/` 当前只记录可选实验边界，不由 Linux CMake 递归构建。
- Orange Pi 部署与 ESP32/STM32 烧录使用各自原生流程，不建立统一超级构建。
- 不直接共享 Linux C++ 类型给 MCU；协议必须显式编码、固定宽度并定义字节序。

# 实施顺序

1. Linux Core：周期线程、`SCHED_FIFO` 可观测降级、绝对时间睡眠、状态机、watchdog、
   trace 和 benchmark。
2. vcan 端到端：CLI/daemon、`epoll`、`signalfd`/`eventfd`、SocketCAN、`vcan` 节点
   模拟器和故障场景。
3. ThinkPad 证据：自动故障矩阵、sanitizer、普通/FIFO 与空载/压力 benchmark。
4. Orange Pi 部署：SSH、systemd、权限、日志、CPU governor/affinity 和 ARM 压力基准。
5. EtherCAT：针对具身机器人系统平台岗位，在 ThinkPad 板载有线网口上先用 SOEM
   和一个简单 I/O SubDevice 验证状态机、PDO、working counter、掉线恢复与周期；
   不从伺服开始。x86 基线关闭后，才决定是否在 Orange Pi 4 Pro 板载网口重复 ARM 对照。
6. Modbus TCP：EtherCAT 基线后做零采购双进程/双机互操作，作为外围工业设备集成能力。
7. 可选实物通信：Modbus RTU、ESP32 USB、physical CAN 只优先选择一条最有证据价值
   的链路；RTU 先用 PTY 学协议帧，再决定是否购买 RS-485 硬件。
8. ROS 2 Adapter 与只读运维接口：协议与 Runtime 生命周期稳定后再接入。
9. PREEMPT_RT 与 EtherCAT DC/servo：分别作为周期对照和高风险进阶实验，不改变 V1
   Runtime 边界。

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
