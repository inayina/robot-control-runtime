# 机器人边缘 Runtime 与设备工程站

## Robot Edge Runtime & Device Commissioning Workbench

> **Self-designed reference robot-cell integration scenario**
> **个人设计的参考机器人工作单元集成场景，不是实际客户交付项目。**

这是一个面向机器人工作单元集成的 ROS-free C++20 Linux edge runtime：它把设备监督、命令
准入、故障恢复、工程站诊断和可回滚部署放在 Orange Pi 边缘侧，而不是把控制权交给 Qt、网络或
上层平台。

## 30 秒了解项目

| Problem | Solution | Evidence boundary |
|---|---|---|
| 设备能动后，谁在 Linux 边缘侧保证命令新鲜、设备可监督、故障可恢复、现场状态可诊断？ | `RuntimeDaemon` + `rcr_cell_app` 在 Orange Pi 保持本地 control authority；ThinkPad Qt 是工程站；CAN 节点和 Modbus RTU I/O 分别接入。 | LD8 关闭的是本机 release candidate；物理闭环验收仍为 `OPEN / DEFERRED`。软件、vcan、loopback 和 physical smoke 不互相升级。 |

```text
ThinkPad Qt engineering workstation
        │ CEL1 / TCP (commissioning and read-only status)
        ▼
Orange Pi rcr_cell_app
  RuntimeDaemon + CellReadyMapper
      │ SocketCAN                  │ localhost TCP
      ▼                            ▼
STM32F103 CAN node            Modbus RTU agent
SG90 + PA0 POSITION_REACHED       /dev/ttyS7 → MR0-IOR08
```

## 从你的视角开始读

| 读者 | 入口 | 你会得到什么 |
|---|---|---|
| System Integration / Solutions | [Solution Case](docs/portfolio/SOLUTION_CASE.md) | 场景、拓扑、接口、commissioning、故障恢复、交付与证据边界 |
| Runtime / Linux Engineering | [Architecture](docs/ARCHITECTURE.md)、[Process / Thread Model](docs/RUNTIME_PROCESS_THREAD_MODEL.md)、[Code Ownership](docs/CODE_OWNERSHIP_MAP.md) | Runtime、线程、fd、SocketCAN 与 ownership 决策 |
| Verification / Delivery | [Acceptance Report](docs/LOCAL_SYSTEMS_ENGINEERING_ACCEPTANCE_REPORT.md)、[Traceability](docs/REQUIREMENTS_TRACEABILITY_MATRIX.md)、[Evidence Index](evidence/README.md) | 已验证范围、可复现入口、开放缺口和交付边界 |

## What I implemented

- `RuntimeDaemon`：状态机、watchdog、session/sequence/deadline command admission、设备监督、
  fault/recovery 语义，以及 Scheduler 与 SocketCAN 生命周期。
- `rcr_cell_app`：物理演示的 edge host；组合 Runtime、CEL1 和 `CellReadyMapper`，并保持
  `can0` 的单一应用 owner。
- STM32F103 peer：CAN V1 heartbeat/status；PA8 两档 SG90 PWM，PA0 对射到位输入映射为
  `POSITION_REACHED`。SG90 仅为低风险演示件。
- Modbus RTU agent：独占 `/dev/ttyS7`，将 MR0-IOR08 的线圈状态呈现为 requested/confirmed，
  避免“请求已发出”被误认为“现场已确认”。
- Qt commissioning workbench：通过 CEL1 观察与下发工程命令；不拥有 CAN fd、serial、
  watchdog、CellReady 或 DO0 自动闭环。
- delivery mechanics：release manifest/hash、health/observability、diagnostics、incident RCA 和
  last-known-good rollback 合同。

## What has been verified

| Evidence level | What it supports | What it does not support |
|---|---|---|
| LD8 local release candidate, baseline `b31296f` | Qt-OFF/Qt-ON 本机构建测试、Operations/rollback loopback、diagnostics、traceability 与静态部署检查 | Orange Pi install/boot、physical CAN/RTU、功能安全、硬实时 |
| vcan / simulator / loopback | Runtime command admission、session/lease、fault handling、接口错误边界 | 实物节点、RS-485 收发或现场执行器 |
| STM32F103 physical CAN smoke | dirty-tree 双向 CAN V1、PC13、SG90 无负载双位置目视与仲裁诊断 | clean hardware acceptance、工业伺服、完整故障矩阵 |
| Closed-loop portfolio checklist | 已采集项的独立证据 | 整表仍未关闭；缺项保持 `NOT_RUN` |

完整分类请读 [Evidence Index](evidence/README.md)；当前项目状态和未来 Gate 选择只读
[plans index](docs/plans/README.md)。

## Design boundaries worth discussing

- `rcrd` 与 `rcr_cell_app` 是同一 `RuntimeDaemon` 的两个宿主。主演示使用后者，因此二者不能
  同时写物理 `can0`。
- CAN 节点 loss 与 RS-485 I/O loss 有独立语义：前者由 Runtime supervision 处理
  CommLoss/Hold/Fault；后者是 Cell I/O `OFFLINE/TIMEOUT`，恢复后必须显式 Probe，不能静默重放
  DO0。
- session、sequence 与 `CLOCK_MONOTONIC` deadline 属于 command admission：旧会话、乱序或排队后过期
  命令不能在重启/恢复后重新生效。
- 软件 E-stop/联锁是软件行为演示，不是功能安全回路；普通 Linux 上的 `SCHED_FIFO` 不等于硬实时。

## Scope and non-claims

- 不声称 ROS 2、EtherCAT、PREEMPT_RT、hard realtime、功能安全、量产控制器或工业伺服。
- 不把 Qt crash isolation、vcan、loopback、静态 unit verify 或 dirty physical smoke 写成完整物理
  验收。
- Portfolio V1 功能冻结；不启动新总线、新 UI、Transport/plugin framework 或 Runtime 大重构。

## Deep technical traceability

| Topic | Authority |
|---|---|
| Scope and hardware boundary | [SPEC.md](SPEC.md) |
| System relationships | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Ownership and dependency direction | [docs/CODE_OWNERSHIP_MAP.md](docs/CODE_OWNERSHIP_MAP.md) |
| `rcrd` lifecycle contract | [docs/RCRD_CONTRACT.md](docs/RCRD_CONTRACT.md) |
| Orange Pi deployment and rollback | [docs/ORANGE_PI_BRINGUP.md](docs/ORANGE_PI_BRINGUP.md) |
| Current project state | [docs/plans/README.md](docs/plans/README.md) |
