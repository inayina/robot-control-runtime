# robot-control-runtime

面向 Orange Pi 4 Pro 4GB 的 ROS-free C++20 Linux Edge Runtime。项目聚焦机器人底层
系统岗位需要的周期调度、Linux fd、SocketCAN、状态监督、可观测性和部署，不重复其他
仓库已经完成的 MCU 电机闭环。

```text
ThinkPad
  ├─ vcan 功能闭环、单测、sanitizer、故障矩阵
  └─ benchmark 对照与开发工作流
            │ git / ssh
            ▼
Orange Pi 4 Pro 4GB
  ├─ ARM 原生构建、release/systemd 安装、调度测量
  ├─ stock：CONFIG_CAN 未启用
  ├─ can1：跑过 vcan0 + rcrd 软件链；不是 physical can0
  └─ can2：普通版 Waveshare HAT ↔ STM32F103 双向 CAN、PC13、SG90 与仲裁实验；不是默认启动或 B4
```

## 当前主线

当前 Active Gate 是
[Closed-Loop Portfolio Freeze](docs/plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)：
把 Orange Pi Linux Edge Runtime、STM32 CAN 节点、对射到位和 MR0 Cell I/O 收成一条
可解释物理闭环。Physical Modbus RTU backend 是前置，不再扩张。
Remote Boundary 保持 `LOOPBACK / NO PHYSICAL PC-ARM`。

本仓核心是部署在 Orange Pi 上的机器人 Linux Edge Runtime（监督、状态机、watchdog、
command freshness、I/O lifecycle、fault recovery）。CAN 是内部节点链路；Qt 是工程
诊断台；Modbus RTU 是低频 Robot Cell 外围 commissioning。

## 已实现

**Runtime**

- `CLOCK_MONOTONIC` 绝对周期调度与可观测 `SCHED_FIFO` 降级；
- Runtime 状态机、软件联锁、watchdog、latest-wins 普通输出邮箱；
- session、sequence、deadline、原子故障升级、单笔在途 ACK 与输出 lease；
- `epoll`、SocketCAN、`eventfd`/`signalfd`、有界输入队列和有界关闭；
- CAN V1 codec、独立节点模拟器、双进程 vcan 验收和故障矩阵。

**部署与证据**

- ThinkPad 单测、ASan+UBSan、TSan 分类、故障矩阵和调度矩阵采集路径；
- Orange Pi 原生构建、release/current/manifest、systemd unit 和 ARM 调度测量；
- Orange Pi stock 与 can1 内核证据分开记录，不把 vcan 写成物理 CAN；
- 可选 can2 内核上完成普通版 Waveshare HAT 的 MCP2515 `can0` probe，并与 STM32F103 bxCAN 做过双向协议、PC13、
  无负载 SG90 双位置目视动作和专用物理仲裁 smoke；全部仍是 dirty-tree 独立证据；
- 结果使用 `pass`、`failed`、`permission_denied`、`unsupported`、`not_run`，Skip 不冒充
  阶段通过。

**可选消费者与独立实验**

- Headless/Qt Device Workbench 是 Runtime 的可选 commissioning/diagnostics 消费者；
- Actuator 01 目前只是 `MOCK / ISOLATED`，不发运动 CAN 帧；
- STM32F103 固件是独立物理实验，不由 Linux CMake 构建，也不自动成为 Qt/Runtime actuator；
- Modbus、EtherCAT 和多总线 observer 保持独立实验，不进入 V1 Runtime Core。

项目不声明硬实时、功能安全、认证急停、真实执行器闭环或完整 physical CAN acceptance。
当前物理结果不包含 PWM 波形、断线/bus-off/IWDG 故障矩阵、`rcrd --can can0` 或 Qt physical
Health；证据边界见 [STM32F103 physical CAN evidence](evidence/stm32f103_can/README.md)。

## 目录

```text
linux/       C++20 Runtime、daemon、测试以及可选 Workbench
protocol/    冻结的 CAN V1 线级合同与 golden vectors
deploy/      Orange Pi release/systemd/bring-up 合同
experiments/ 独立实验；不由 linux/ 递归构建
firmware/    可选 MCU 实验边界；V1 不构建
evidence/    可复现证据与脱敏摘要
docs/        架构、原理、计划、Workbench 和作品集材料
```

`linux/src/` 按 `core`、`can`、`linux`、`runtime`、`supervision`、`daemon`、`sim`
表达职责归属；不把目录当成必须逐层抽象的依赖框架。Workbench 在自己的
`application/services/profile` 目录内组织，不反向拥有 Runtime 状态或 CAN fd。

## 从哪里继续

| 想了解什么 | 从这里开始 |
|---|---|
| Architecture | [系统边界](SPEC.md) → [Runtime 架构](docs/ARCHITECTURE.md) → [代码 ownership](docs/CODE_OWNERSHIP_MAP.md) |
| Run / Build | [构建与测试](#构建与测试) → [最小运行路径](#最小运行路径) |
| Hardware / Orange Pi | [bring-up 与部署合同](docs/ORANGE_PI_BRINGUP.md) → [部署资产](deploy/orangepi/README.md) |
| Verification | [证据入口](evidence/README.md) → [证据 schema](docs/EVIDENCE_SCHEMA.md) |
| Workbench | [Workbench 主入口](docs/workbench/README.md) |
| Development Roadmap | [计划角色与当前 Gate](docs/plans/README.md) |
| Portfolio | [作品集入口](docs/portfolio/README.md) |

完整但仍按任务组织的文档入口见 [docs/README.md](docs/README.md)，仓库区域速查见
[docs/REPOSITORY_MAP.md](docs/REPOSITORY_MAP.md)。学习与面试材料从
[docs/KNOWLEDGE_BASE.md](docs/KNOWLEDGE_BASE.md) 进入，不在 README 复制。

## 构建与测试

默认构建 Runtime 和无 Qt Workbench；Qt UI 默认关闭。

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

需要 Qt6 UI 时显式打开：

```bash
cmake -S linux -B build/qt-on \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON
cmake --build build/qt-on -j2
ctest --test-dir build/qt-on --output-on-failure
```

## 最小运行路径

创建 `vcan0` 需要主机权限；库和 daemon 不会自行修改网络接口。

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
```

终端 A 启动节点模拟器，终端 B 启动 daemon：

```bash
./build/linux/rcr_node_sim --can vcan0 --node-id 1 --duration-ms 2000
./build/linux/rcrd --can vcan0 --node-id 1 --duration-ms 1000
```

阶段验收必须显式要求 vcan；缺接口不能用 Skip 凑 PASS：

```bash
./build/linux/tests/test_socketcan_vcan --require-vcan
./linux/scripts/run_vcan_acceptance.sh vcan0
./linux/scripts/run_fault_matrix.sh vcan0
```

周期 benchmark 测量空 callback 的唤醒 lateness，不是 CAN/control 端到端延迟：

```bash
./build/linux/rcr_benchmark \
  --duration-ms 10000 --period-us 1000 --samples-out /tmp/rcr-samples.txt
```

Workbench 的运行、线程、Mock 边界和证据见
[docs/workbench/README.md](docs/workbench/README.md)，不在顶层 README 复制维护。

## 下一步

停在下一 Gate 选择点。先读 [审计与候选比较](docs/SYSTEM_CONVERGENCE_AUDIT.md) 和
[PC→ARM→Device 长期计划](docs/plans/PC_ARM_DEVICE_CONVERGENCE_PLAN.md)，再明确选择 physical
RS-485、物理 PC–ARM Remote、V1 clean/physical CAN remaining acceptance 或 EtherCAT independent
experiment 中的一项。未选择前不新增通信实现。
