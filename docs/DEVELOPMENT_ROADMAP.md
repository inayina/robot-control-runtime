# 后续开发路线

状态：Active  
更新日期：2026-08-03
当前进度：P3-A0（部署路径/manifest/回滚合同）已落地；先关闭 A-T/B 的两个小型证据缺口，
随后进入 P3-A1 systemd unit。
P3-G0 sanitizer 脚本修复已本地验证；运行产物按用户决定不提交。

本路线以退出条件而不是技术数量衡量进度。V1 先形成 Orange Pi 可部署的 Linux Runtime，
协议和硬件实验随后独立推进；后续实验不能反向要求 Runtime Core 提前建立通用 Transport。

稳定的职责坐标与“每阶段只研究一个核心矛盾”的 A–G Gate 见
[“五层一横”架构与 A–G 证据路线](FIVE_LAYERS_ONE_PLANE.md)。本文保留旧阶段编号，作为
已有证据和后续 EtherCAT/Modbus 分支的历史路线，不与 A–G 重复编号。

阶段 2～4（执行编号 P1～P3）的工作包、线程/fd 所有权、测试 Gate 和提交边界见
[P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md)。本路线图负责长期顺序，详细计划负责近期执行。

## 1. 路线总览

```text
当前 Linux Core + CAN V1 + rcrd（P1/P2 已实现并审计）
      ↓
Orange Pi 4 Pro 4GB SSH + systemd + ARM 实测（P3）
      ↓
EtherCAT MainDevice + simple I/O SubDevice
      ↓
Modbus TCP（外围设备集成）
      ↓
physical CAN / Modbus RTU / ESP32 USB（三选一优先）
      ↓
ROS 2 Adapter / read-only Dashboard
      ↓
PREEMPT_RT 对照 / EtherCAT DC或伺服进阶
```

### 1.1 学习顺序不是“物理 CAN 完成后才能学 Modbus”

实际顺序是：

```text
SocketCAN/vcan 软件链 → Orange Pi 部署
        ↓
具身系统/控制平台方向：EtherCAT I/O 实物闭环
        ↓
Modbus TCP 零采购补充
        ↓
按目标岗位选择 physical CAN 或 Modbus RTU
```

因此 CAN 先学的是 Linux SocketCAN 和事件驱动行为，不要求先购买 CAN 硬件。对具身
机器人系统平台岗位，EtherCAT 的执行链价值高于 Modbus，应在 Orange Pi 部署后优先，
首轮 EtherCAT 主站使用 ThinkPad 的板载有线网口，不绑定到 Orange Pi。该基线关闭后，
可以在 4 Pro 的板载千兆网口上重复 ARM 对照，但不能混写平台结论。Modbus TCP 随后通过
现有管理网络学习；physical CAN 与 physical RS-485 是并列补充分支。

### 1.2 统一硬件场景：机器人单元边缘 I/O 与状态网关

这个场景不重复电机闭环：Orange Pi 是机器人单元 Edge Runtime，对本机内部节点做
实时性较高的状态监督，同时对工业设备做低频配置和状态采集。EtherCAT 首轮是
ThinkPad 上的独立主站实验，不表示三条总线已经同时接入 Orange Pi Runtime。

```text
Robot Cell Experiments
├─ ThinkPad Linux ── dedicated Ethernet ── EtherCAT I/O SubDevice
└─ Orange Pi Runtime
   ├─ future physical CAN ── ESP32 node
   └─ Modbus TCP/RTU ─────── PLC/仪表/外围驱动器或模拟器
```

这是一张职责图，不表示首版必须同时接多条总线，也不表示 EtherCAT 已经接入 Runtime。
每次实验只启用一条链路，独立通过 Gate 后再评估 Adapter。

#### EtherCAT 实物场景

```text
Wi-Fi：Internet/管理
        │
ThinkPad EtherCAT experiment
        └─ onboard Intel Ethernet ── EtherCAT I/O SubDevice
                                           ├─ digital input
                                           └─ digital output / status
```

EtherCAT 模拟具身机器人计算平台与执行层/分布式 I/O 的确定性周期数据交换。首个从站
选择有明确 ESI、PDO 和状态机资料的简单 I/O SubDevice，不买伺服，不接电机；先验证
INIT/PREOP/SAFEOP/OP、PDO、SDO、working counter、掉线与恢复。ThinkPad 的板载有线
网口必须专用于 EtherCAT，管理走 Wi-Fi；先验证网卡 raw frame 和驱动行为，再记录周期
证据，不能先承诺工业实时性能。Orange Pi 4 Pro 有板载千兆网口，但不承担首轮主站；
后续 ARM 对照时该网口独占 EtherCAT，管理连接走 Wi-Fi。

#### CAN 实物场景

```text
Orange Pi 4 Pro
  └─ explicit SocketCAN interface + transceiver
          ║ twisted pair + two 120 Ω terminations
  └─ ESP32-S3 + external 3.3 V CAN transceiver
          ├─ heartbeat / boot counter / fault status
          └─ 普通输出命令 → 板载 RGB LED 或外接低功耗 LED
```

它模拟机器人内部“主控 ↔ 分布式 I/O/诊断节点”，重点测 SocketCAN、arbitration、
error counter、bus-off、端接、断线和恢复，不做电机。若选 USB-CAN，重点是稳定的
SocketCAN 端到端证据；若选 SPI CAN，才增加设备树、pinmux 和中断验证。ESP32-S3 有片上
TWAI 控制器，但没有片上 CAN 收发器，所以实物总线必须增加外部收发器。板载 RGB LED
的引脚随 DevKitC-1 硬件版本可能不同，拿到准确板卡版本后再冻结，不凭型号后缀猜测。

#### Modbus TCP 场景

```text
Orange Pi Modbus client ── existing LAN ── ThinkPad reference server
        Runtime/adapter                         PLC/remote-I/O simulator
```

它模拟机器人边缘机轮询 PLC、远程 I/O、仪表或驱动器的状态/参数。寄存器可表达设备
状态、fault code、boot counter、配置目标和普通输出，但 Modbus 轮询不进入 1 ms 闭环。
首版不需要购买 PLC，也不需要 ESP32；ThinkPad server 是互操作对端。

#### Modbus RTU 实物场景

```text
Orange Pi ── USB-RS485 ── twisted pair ── 3.3 V RS-485 transceiver ── ESP32-S3
 client                                                        server
```

它模拟边缘机轮询传统 RS-485 远程 I/O、能耗表或驱动器参数。ESP32 暴露与 TCP 实验相同
业务含义的寄存器，但 RTU 映射单独实现，不把 CAN 与 Modbus 强行塞进一个 Transport。
重点验证 CRC16、DE/RE 半双工方向、baud/parity、帧间隔、timeout、端接和断线。

官方硬件依据：

- [ESP32-S3 TWAI：需要外部 CAN transceiver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/twai.html)
- [ESP32-S3 UART：支持 RS-485 half-duplex driver mode](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/peripherals/uart.html)
- [ESP32-S3-DevKitC-1 硬件版本资料](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html)

## 2. 阶段状态

| 阶段 | 内容 | 当前状态 | 是否需要 Orange Pi |
|---|---|---|---|
| 0 | Linux Core | 已有首版，持续加固 | 否；到货后复测 |
| 1 | CAN V1 codec 与节点模拟器 | 已完成，关闭里程碑 | 否 |
| 2 | `rcrd`、epoll 与有界退出 | 实现完成；本地验收通过 | 否 |
| 3 | 故障矩阵、sanitizer 与 ThinkPad benchmark | 实现完成；报告重跑缺陷已本地修复，clean 证据待补 | 否 |
| 4 | SSH、systemd、权限与 ARM benchmark | P3-A0 完成；A1/B 待做 | 是 |
| 5 | EtherCAT MainDevice + simple I/O SubDevice | 部署后优先 | 否；使用 ThinkPad，但需要从站 |
| 6 | Modbus TCP 学习实验 | EtherCAT 基线后 | 推荐，但可先本机 |
| 7 | Modbus RTU / physical CAN / ESP32 USB | 可选、三选一优先 | 物理阶段需要少量硬件 |
| 8 | ROS 2 Adapter 与只读运维接口 | 远期 | 是 |
| 9 | PREEMPT_RT 与 EtherCAT 周期对照 | 远期 | 是 |
| 10 | EtherCAT DC / servo drive 进阶 | 独立远期实验 | 需要相应从站与风险评审 |

## 3. 阶段 0：加固 Linux Core

已实现 Scheduler、StateMachine、Mailbox、Watchdog、Trace、SocketCAN、FakeCanBus、
vcan 辅助和 EpollReactor。

原列入本阶段的 scheduler worker 异常升级、有界输入队列、overflow fault、重复
start/stop 和并发状态迁移测试，已经随 P1/P2 实现。剩余加固转成“五层一横”的明确
边界债务和证据缺口：

- `PeriodicScheduler`、单调时钟实现已经归入 `src/linux/`，组合对象 `LinuxRuntime`
  已归入 `src/daemon/`；公共 API 与行为未改变；
- `NodeSupervisor` 当前通过 `LinuxRuntime&` 驱动状态，只有出现真实第二调用者或修改该
  接口时才收窄依赖，不提前建立通用监督接口；
- ASan/UBSan 已本地通过；TSan 在当前主机为 `unsupported`，不能据此关闭并发证据边界；
- A-T 受控 callback 过载与 B 的 fd/线程稳定、显式授权接口 down 用例已落地。

功能退出条件已经满足；跨平台测量和上述证据缺口按 A–E Gate 分别关闭。

## 4. 阶段 1：CAN V1 与独立节点模拟器

目标：让现有 SocketCAN 组件经过真正的进程间内核路径，而不是只停留在类级单测。

### 最小消息

- `NodeHeartbeat`
- `NodeStatus`
- `OutputCommand`
- `OutputStatus`

Fault Injection 不进入正式 CAN 消息；模拟器通过启动参数选择丢 heartbeat、延迟、
重启、乱序或非法帧场景，避免在生产协议中保留主动制造故障的入口。

### 设计选择

- 选择经典 CAN 8-byte 帧和显式编解码，便于未来迁移到物理 CAN。
- 不选 ISO-TP：当前消息能通过紧凑字段或职责拆分表达，没有长报文需求。
- 不直接发送 `rcr::OutputCommand` 内存布局：C++ 对齐、字节序和 64 位字段不能成为
  线协议。
- 不把 simulator 链接进 Runtime：独立进程才能发现 socket、过滤、丢帧和生命周期问题。

退出条件：golden vectors 固定；两个独立进程只经 `vcan0` 完成正常、重启、旧会话、
乱序、超时和非法帧测试。

## 5. 阶段 2：可部署 Runtime daemon

状态：**Implementation complete / latest local key scenarios passed**
详细工作包见 [P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md) 的 P1 部分；
进程合同见 [RCRD_CONTRACT.md](RCRD_CONTRACT.md)；证据见 `evidence/rcrd_acceptance/`。

目标线程模型：

```text
main/Application
    └─ 配置、生命周期、状态查询

PeriodicScheduler
    └─ watchdog/deadline/heartbeat supervision

I/O thread
    └─ epoll(SocketCAN, eventfd, signalfd)
```

- `eventfd` 负责内部停止唤醒；`signalfd` 把 SIGINT/SIGTERM 纳入 fd 生命周期。
- 周期线程不做 socket I/O 或磁盘日志。
- 第一版不建线程池：当前只有一个 CAN fd 和少量生命周期 fd，单 I/O 线程更容易分析
  顺序和关闭行为。
- 第一版不做 REST、ROS 2 或复杂 `rcrctl`；先通过配置和测试场景驱动 daemon。

已验证重复启动/停止能够正常回收子进程、SIGTERM 有界退出、节点离线进入可见 Fault，
进程行为可供后续 systemd 复用。同进程 100 次启停断言 `/proc/self/fd` 与 `Threads:`
回到基线；进程级测试采样运行中子进程 fd 并断言父进程不增长。接口 down 通过
`RCR_ALLOW_IFACE_DOWN=1` + `sudo ./linux/scripts/run_vcan_iface_down_fault.sh` 显式授权验证。
**未完成**：Orange Pi systemd 部署（阶段 4 / P3）。

## 6. 阶段 3：ThinkPad 证据基线

状态：**Implementation complete / local matrix reviewed / report continuity fix pending**
详细工作包见 [P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md) 的 P2 部分；
schema 见 [EVIDENCE_SCHEMA.md](EVIDENCE_SCHEMA.md)。

测试矩阵至少包含：

- 正常、过期、重复和倒退 sequence；
- 旧 session、节点重启、heartbeat 中断和恢复；
- mailbox 覆盖、事件队列溢出和 scheduler worker 异常；
- 普通调度与 `SCHED_FIFO`，空载与 `stress-ng`；
- 1 ms、5 ms、10 ms 周期的 P50/P95/P99/P99.9 和 deadline miss。

ThinkPad 数据是对照，不代表 Orange Pi 或硬实时结果。缺 `stress-ng` 时压力格必须记
`unsupported`，不得写成 PASS。

本地 `342fb0d` 已得到 19/19 故障场景和 12/12 benchmark 格。sanitizer 重跑 0 字节报告
缺陷已在脚本侧修复（`mktemp -d` + 同目录原子 rename + `秒.PID` 文件名）；连续两次
ASan/UBSan=`pass`、TSan=`unsupported`。正式 clean-commit 证据与 rcrd 重复启停仍属
P3-G0 收尾项。运行产物按用户决定可不提交。

## 7. 阶段 4：Orange Pi 部署

详细工作包见 [P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md) 的 P3 部分；
路径合同见 [ORANGE_PI_BRINGUP.md](ORANGE_PI_BRINGUP.md)。

P3-A0 已冻结 `/opt/robot-control-runtime/releases/<sha>`、`current`、用户 `rcr`、
MANIFEST 与 dry-run 安装/回滚。后续按顺序完成：

1. P3-A1 systemd unit 与静态验证；
2. 以 Orange Pi 4 Pro 4GB 为目标完成 P3-A2 清单；
3. 到货后记录准确板卡、电源、启动介质、OS、内核、设备树、CPU 拓扑和编译器；
4. SSH 密钥登录、网络和时间同步；
5. 板上原生 CMake 构建并运行全部测试；
6. 安装 release、启用 unit、普通服务用户、日志和重启限制；
7. 最小化实时调度权限，不让服务长期以 root 运行；
8. 同条件重复阶段 3 benchmark，比较 x86_64 与 aarch64；
9. 正常重启、SIGTERM、进程崩溃、session 更新与 release 回滚验收。

退出条件：冷启动后 daemon 可用；停止有界；压力数据可复现；FIFO 是否真正生效可观测。

## 8. 阶段 5：EtherCAT 主站与 I/O 从站

### 8.1 对具身智能求职的价值

| 目标岗位 | EtherCAT 优先级 | 原因 |
|---|---|---|
| 具身策略、视觉、VLA 算法 | 低～中 | 会读状态和定位链路问题即可，核心仍是算法与数据 |
| 运动控制、WBC、执行器集成 | 高 | 多轴周期数据、同步、驱动器状态与故障直接相关 |
| 机器人计算平台、实时系统 | 高 | Linux 主站、实时线程、网卡、故障恢复是核心职责 |
| MCU/驱动器嵌入式 | 中～高 | 更偏 EtherCAT SubDevice/ESC、CAN/CAN-FD 与本地控制 |
| 现场部署与系统集成 | 中～高 | ESI/PDO、布线、状态机和互操作决定交付效率 |

本用户的其他仓库已经覆盖策略数据、仿真、ROS 2、导航和 MCU 电机 bench。本仓选择
“机器人计算平台/实时系统”作为补充方向，因此 EtherCAT 排在 Modbus 前；如果以后
只投纯算法岗，可停在原理与基本主站调试，不继续购买伺服。

当前招聘样本也体现这种岗位分化：同一机器人公司的运动算法岗位强调运动学与轨迹，
Linux 系统岗位则明确要求实时性能和 EtherCAT 主站可靠性测试，可见
[Elephant Robotics careers](https://www.elephantrobotics.com/en/careers/)。路线据此增强
执行链能力，但不把单个招聘描述当成行业统一标准。

### 8.2 为什么不一开始做 EtherCAT

EtherCAT 需要独占有线网口和真实 SubDevice 才能形成有意义的证据。当前先完成 daemon、
故障模型、Orange Pi 权限和 benchmark，是为了复用已经验证的生命周期与 trace；否则
EtherCAT 安装、内核、网卡和应用状态会同时变化，故障难以定位。

与 CAN/Modbus 不同，软件 loopback 无法充分替代 EtherCAT SubDevice 的 ESC、状态机、
PDO 和 distributed clock 行为，所以最终验收必须有一个真实从站。

### 8.3 最小硬件场景

```text
management: Wi-Fi
             │
       ThinkPad P14s Gen 6
             └─ onboard Intel NIC / e1000e（EtherCAT 专用）
                         │ direct Ethernet cable
             documented EtherCAT I/O SubDevice
                         ├─ one digital input
                         └─ one ordinary digital output
```

最小 BOM 在购买前单独评审：

- 已有带板载有线网口的 ThinkPad；
- 一个资料完整、最好具有 ETG conformity/certification 信息的 EtherCAT I/O SubDevice；
- 从站要求的电源，工业 I/O 常见为 24 V，但必须以具体型号手册为准；
- 网线、端子、一个普通按钮/LED 或低功耗测试负载；
- 若模块化耦合器要求 end terminal/power terminal，必须纳入完整 BOM。

现有 ESP32-S3 和 STM32F103 不能只靠增加 RJ45 或普通 Ethernet PHY 就成为合格的
EtherCAT SubDevice；EtherCAT 的 on-the-fly frame processing 需要 ESC（ASIC、FPGA、
集成 ESC 的 MCU 或通信模块）。首阶段直接购买资料完整的 I/O SubDevice，比再开发一套
ESC + SubDevice stack 更符合“学主站与系统集成”的目标。

不买伺服的原因：首阶段只需要验证主站、状态机、PDO 和故障恢复；伺服会额外引入高压/
动力、编码器、制动、参数整定和机械风险，不能提高主站基础学习效率。

### 8.4 软件栈选择

1. 先用 [SOEM](https://github.com/OpenEtherCATsociety/SOEM)：它是轻量的用户态 C 主站
   库，适合理解网卡 raw frame、从站扫描、状态迁移、PDO 和 working counter。
2. 再评估 [IgH EtherCAT Master](https://etherlab.org/en_GB/ethercat)：它以 Linux
   kernel master 和应用接口为主，更适合继续研究实时扩展、domain/process data 和
   网卡驱动差异。

不自写 EtherCAT 主站栈：协议状态、mailbox、FMMU、SyncManager、CoE 和兼容性范围太大，
自研不会比理解并正确使用成熟主站更能证明机器人系统能力。不先用 TwinCAT 作为主线，
因为本项目要证明 Linux 主站与实时行为；TwinCAT 可作为从站配置或抓包
对照工具，但不是本仓运行时依赖。

### 8.5 学习与实现顺序

1. EtherCAT frame、on-the-fly processing 和自动寻址；
2. `INIT → PREOP → SAFEOP → OP` 状态机；
3. ESI/SII、SyncManager、FMMU 与 PDO mapping；
4. SDO/CoE 配置路径与 cyclic PDO 数据路径的职责区别；
5. working counter、AL status、从站掉线和恢复；
6. 周期线程、`SCHED_FIFO`、CPU affinity、`mlockall` 与 trace；
7. 从 10 ms 开始，再测 2 ms/1 ms；周期只能依据实测收紧；
8. 具备 DC-capable SubDevice 后再研究 distributed clocks，不用主站唤醒 jitter 冒充
   从站同步精度。

### 8.6 EtherCAT 主站网卡 Gate

- 当前 ThinkPad P14s Gen 6 检测到 `enp0s31f6`，由 Intel `e1000e` 驱动；实验前仍需把
  PCI ID、内核和驱动版本写入报告，避免把一次探测结果当成永久配置。
- 有线 NIC 必须专用于 EtherCAT；管理和互联网连接走 Wi-Fi。
- 先确认接口支持 raw frame，并确保 NetworkManager/DHCP 不干扰该专用接口。
- SOEM 能扫描并进入 OP 只证明功能可用，不证明周期确定性。
- 普通内核建立周期基线后再测试 PREEMPT_RT。
- 若板载 NIC/驱动的抖动或恢复行为不满足实验目标，再评估兼容性明确的独立 PCIe NIC
  或工业 x86 主机；不先买硬件，也不为了维护“某块板必须做主站”的叙事篡改数据。
- Orange Pi 4 Pro 的板载千兆网口可以在 ThinkPad Gate 关闭后用于 ARM/SOEM 对照；必须
  单独记录 PHY、驱动、IRQ、内核、CPU affinity 和管理网络路径，不能与 ThinkPad 的
  板载 PCIe NIC 混为同一组 benchmark。Surface 保持 Windows，不把 WSL2 或 USB 网卡
  纳入 Linux EtherCAT 实时性证明。

### 8.7 退出条件

- 扫描真实 SubDevice，保存 vendor/product/revision 和 ESI；
- 正确经过 INIT/PREOP/SAFEOP/OP，并能解释每个状态允许的行为；
- 循环读一个 input、写一个 ordinary output，记录 PDO offset 和 working counter；
- 自动验证拔线、从站掉电、WKC 异常、恢复和 daemon SIGTERM；
- 记录普通/FIFO 下周期分布，明确是否使用 DC；
- 不把普通数字输出、软件停止或 EtherCAT 通信本身描述为安全功能。

官方学习依据：

- [ETG EtherCAT technology](https://www.ethercat.org/en/technology.html)
- [ETG EtherCAT Compendium](https://www.ethercat.org/en/compendium.htm)
- [ETG SubDevice Implementation Guide](https://www.ethercat.org/en/downloads/downloads_7BA2567EB9F443219AD0014448F674F2.htm)
- [SOEM](https://github.com/OpenEtherCATsociety/SOEM)
- [IgH EtherCAT Master documentation](https://docs.etherlab.org/ethercat/1.6/pdf/ethercat_doc.pdf)

## 9. 阶段 6/7：Modbus 学习路线

Modbus 不属于 V1，但适合作为 Orange Pi 部署完成后的工业协议实验。学习顺序固定为
Modbus TCP → Modbus RTU，原因是 TCP 阶段零采购且能先掌握共同的 PDU、数据模型、
function code 和 exception；RTU 再单独增加串口时序、CRC 与 RS-485 半双工问题。

### 9.1 先学共同应用层

先掌握：

- Coil、Discrete Input、Input Register、Holding Register 四类数据；
- protocol address 与常见 `4xxxx` 文档编号的差异，寄存器表必须明确零基地址；
- request/response、function code 和 exception response；
- 16 位寄存器上的字节序，以及 32 位数/浮点数组合属于设备合同而非协议统一保证。

第一批只实现：

- `0x03` Read Holding Registers；
- `0x06` Write Single Register；
- `0x10` Write Multiple Registers；
- 对应 exception response。

不一开始实现全部 function code，因为没有设备需求的代码只会扩大测试面。

### 9.2 Modbus TCP：零采购阶段

拓扑：

```text
到货前：client process ── 127.0.0.1:1502 ── reference server process

到货后：Orange Pi client ── LAN:1502 ── ThinkPad reference server
```

学习端口使用 `1502`，避免为了绑定标准端口 502 让实验进程以 root 运行。

重点：

- MBAP header、transaction ID、length、unit identifier 与 PDU；
- TCP 半包/粘包，不能假设一次 `recv` 等于一个 Modbus ADU；
- connection timeout、response timeout、断线、指数退避和重新连接；
- 第一版每连接只允许一个 outstanding request，先保证确定性和错误归属；
- 用 `tcpdump`/Wireshark 对照原始字节和应用日志。

实现策略采用“两层验证”：

1. 在独立 `experiments/modbus_tcp/` 中手写最小 MBAP/PDU codec 和解析测试，用于理解
   边界检查、事务号和异常响应；
2. 使用 `libmodbus` 作为对端和互操作参考，交叉验证读写结果与错误行为。

不只调用 `libmodbus`，因为那会隐藏协议关键点；也不把教学 codec 直接当生产协议栈，
因为完整兼容性和异常面不值得在本项目重复维护。是否把 Modbus 接入 Runtime，必须在
实验完成后根据真实设备需求另作决策。

退出条件：自写 client/reference server 与 libmodbus 双向互操作；覆盖半包、非法 length、
transaction 不匹配、exception、超时和重连；抓包与 golden vectors 一致。

### 9.3 Modbus RTU：先伪终端，后 RS-485

第一步使用 Linux PTY 对模拟串口，学习：

- address、function、data、CRC16；
- baud/parity/data bits/stop bits 和响应 timeout；
- 帧边界、CRC 错误、错误地址和无响应设备。

PTY 不能证明 3.5 character time、电气噪声、终端、偏置或半双工方向控制，因此只能作
协议测试，不能作为 RS-485 实物证据。

如果确实需要实物，最小采购再评审为：一个 Linux USB-RS485 适配器、一个适配
ESP32-S3 的 3.3 V RS-485 收发器、双绞线和两端端接。ESP32 只做 Modbus RTU server，
不同时开发 F103。此阶段验证 DE/RE 方向、半双工争用、断线、CRC 错误和不同波特率。

退出条件：PTY 自动测试通过；若购买硬件，则同一寄存器合同在真实 RS-485 上互操作，
并明确区分协议证据与电气证据。

### 9.4 Modbus 不承担的职责

- 不用于 1 ms 机器人闭环或硬件急停；
- 不用寄存器轮询替代 Runtime 内部状态机；
- 不因 TCP/RTU 两种形式就提前抽取整个项目的通用 Transport；
- 不把 Modbus TCP 直接暴露到不可信网络；安全接入需另做网络分区和威胁模型。

官方学习依据：

- [Modbus Application Protocol Specification V1.1b3](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [Modbus Messaging on TCP/IP Implementation Guide V1.0b](https://modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf)
- [Modbus Serial Line Protocol and Implementation Guide V1.02](https://modbus.org/docs/Modbus_over_serial_line_V1_02.pdf)
- [libmodbus reference](https://libmodbus.org/reference/)

## 10. 阶段 7：可选外围实物通信

Orange Pi 部署完成后只优先选择一个：

- ESP32 USB：零新增采购，验证真实节点拔插、重启、CRC 和 watchdog；
- physical CAN：只有需要位时序、错误计数、bus-off 和波形证据时采购；
- Modbus RTU：只有岗位或真实设备明确需要 RS-485 时采购。

不同时展开三条链路。选择依据是目标岗位缺口和能够产生的实测证据，不是手里有什么板。

## 11. 阶段 8～10：更远阶段

### ROS 2 Adapter 与 Dashboard

ROS 2 Adapter 只做 Topic/API 转换；Dashboard 只读。先证明 Runtime daemon 稳定，再接
上层，避免 ROS/Web 生命周期掩盖底层问题。

### PREEMPT_RT

只在 Orange Pi 普通内核四组 benchmark 完整后做同条件对照。若没有可测收益，不因
“实时”标签更换内核。

### EtherCAT DC / servo drive 进阶

只在 I/O SubDevice 基线完成后考虑 DC-capable I/O 或 servo drive。进阶目标是理解
distributed clocks、CiA 402/CoE、同步 setpoint 和多轴故障，而不是展示电机转动。
伺服涉及供电、制动、参数和机械风险，必须另做预算、安全和测试计划；没有条件时停在
I/O 从站阶段已经足以证明 Linux EtherCAT 主站基础能力。

## 12. 立即执行顺序

Orange Pi 4 Pro 到货前按以下顺序推进：

1. A-T：已为 `rcr_benchmark` 增加默认关闭的 `--callback-delay-us`，并用单测/本地实验验证
   miss 与跳过旧边界；正式 clean-commit 证据仍按需重采；
2. B：fd/线程稳定与显式授权接口 down 用例已落地；需要时跑
   `sudo ./linux/scripts/run_vcan_iface_down_fault.sh`；
3. P3-A1：实现并静态验证 systemd unit；
4. P3-A2：完成 4 Pro bring-up 清单、证据模板和共享 benchmark runner；
5. 保留已经审计的 ThinkPad 19/19 故障矩阵与 12/12 benchmark 作为 x86 对照；
6. 不提交用户已决定不入库的本轮运行产物；clean-commit 证据在需要形成正式基线时重采。

本轮代码审计、阶段 0 关闭项以及阶段 1 的详细工作包和验收命令见
[当前阶段审计与开发计划](CURRENT_PHASE_PLAN.md)。如两份文档出现执行粒度差异，路线图
决定长期先后关系，当前阶段计划决定近期工作包；系统边界仍以 SPEC 和 AGENTS.md 为准。

Orange Pi 4 Pro 到货并完成阶段 4 后，在 ThinkPad 上执行 EtherCAT I/O SubDevice Gate。
Modbus TCP 可先阅读协议和做一次最小互操作练习，但正式代码排在 EtherCAT 基线之后，
避免多个协议同时争夺主线。
