# Robot Cell Edge Runtime & Device Integration Solution Case

> **Self-designed reference robot-cell integration scenario**
> **个人设计的参考机器人工作单元集成场景，不是实际客户交付项目。**

## 1. Scenario / Problem

设想一个小型机器人工作单元：机器人节点通过 CAN 上报状态，边缘机根据本地 Runtime 状态和到位
信号决定单元外围 I/O，工程师需要在不抢占现场总线的前提下完成 commissioning、状态观察、问题
定位和 release rollback。

这个案例解决的不是运动规划、PID 或 ROS 2 任务编排，而是“设备已经能动之后，Linux edge
runtime 如何保持可监督、可准入、可恢复和可交付”。范围以 [SPEC.md](../../SPEC.md) 为准。

## 2. Stakeholders / Operator

以下是参考场景中的角色，不代表真实客户或真实组织：

| Role | Needs | Boundary |
|---|---|---|
| Commissioning engineer | 观察 Runtime、节点、Cell I/O；下发明确工程命令 | 使用 Qt/CEL1，不打开 CAN 或 serial |
| Edge runtime owner | 保持本地 control authority、会话/时限检查、设备监督和故障状态 | 不把控制决策交给网络、Qt 或 Platform |
| Maintenance / operations engineer | 核对 release identity、health、日志、bundle 和 rollback | 不把 health 字段误读为 physical acceptance |

## 3. Requirements

本案例的可追溯需求是：拒绝过期/重放命令、明确 device loss 语义、保持本地 authority、避免
restart 后误用旧 session/ACK、支持 release rollback，并把 process/service/runtime/device/cell-I/O
健康度分开报告。逐行实现、测试与证据见
[Requirements / Verification Traceability Matrix](../REQUIREMENTS_TRACEABILITY_MATRIX.md)。

## 4. Constraints

- 现场主控是普通 Linux，不宣称 hard realtime 或功能安全。
- Qt 是可选工程站，不是 Runtime 或现场总线 owner。
- CAN V1 与 Modbus RTU 是不同设备域；不引入通用 Transport 抽象来掩盖差异。
- V1 功能冻结：不新增 EtherCAT、ROS 2、PREEMPT_RT、新 UI、新总线或新仓库。
- SG90 是低风险演示件；PA0 是离散到位输入，不是编码器反馈。

## 5. Solution Architecture

```text
ThinkPad Qt Workbench
    │ CEL1/TCP: commissioning command + read-only status
    ▼
Orange Pi: rcr_cell_app
    ├─ RuntimeDaemon: state, watchdog, admission, supervision
    ├─ CellReadyMapper: edge application policy
    ├─ CanIoLoop: sole physical can0 owner
    └─ localhost Modbus agent client
          │                         │
          ▼                         ▼
    SocketCAN / CAN V1          /dev/ttyS7 / Modbus RTU
          │                         │
    STM32F103 peer                 MR0-IOR08
    PA8 SG90, PA0 input             DO0 requested/confirmed
```

系统关系见 [Architecture](../ARCHITECTURE.md)，线程、阻塞点和关闭顺序见
[Process / Thread Model](../RUNTIME_PROCESS_THREAD_MODEL.md)。

## 6. Hardware / Software Topology

| Segment | Implemented role | Evidence boundary |
|---|---|---|
| ThinkPad + Qt | 工程站、CEL1 client、presentation | 不拥有 CAN/serial；不证明 crash isolation |
| Orange Pi + `rcr_cell_app` | 演示 Runtime host、CAN owner、CellReady edge policy | physical acceptance 仍未整体关闭 |
| SocketCAN + CAN V1 | Linux fd/epoll I/O 与固定线级合同 | vcan 和 simulator 不等于 physical CAN |
| STM32F103 | heartbeat/status、PA0 `POSITION_REACHED`、PA8 SG90 demo | SG90 smoke 不是工业执行器验收 |
| Modbus agent + UART7 | serial owner、Probe/read/write 的现场 I/O 边界 | loopback 不等于 physical RTU/断线恢复 |
| MR0-IOR08 | 单元外围 DO0 的真实设备语义 | requested/confirmed 不是功能安全证明 |

硬件选择、引脚和停止线见 [Hardware Topology](../HARDWARE_TOPOLOGY.md)。

## 7. Interface Matrix

| Interface | Producer → consumer | Contract / meaning | Owner |
|---|---|---|---|
| CEL1/TCP | Qt → `rcr_cell_app` | 工程命令、只读 Runtime/Cell status | `rcr_cell_app`；Qt 仅适配 |
| CAN V1 / SocketCAN | `rcr_cell_app` ↔ STM32F103 | heartbeat、status、ordinary output + ACK | `CanIoLoop` owns socket/fd |
| PA0 → NodeStatus | STM32 → Runtime snapshot | `input_bits` bit0 = `POSITION_REACHED` | STM32 owns signal interpretation |
| CellReady policy | Runtime snapshot → mapper | online + Active + position + no fault 的边缘映射 | `CellReadyMapper` |
| Modbus RTU | agent ↔ MR0 | FC01/02/05 现场 I/O transaction | agent owns `/dev/ttyS7` |
| health / release | operations scripts → operator | manifest, version, availability, diagnostics | operations layer, not Runtime authority |

`CellReady` 语义、DO0 边缘 ownership 和 Qt 的只读边界以
[Architecture](../ARCHITECTURE.md) 为准。

## 8. Ownership / Authority

`RuntimeDaemon` 拥有状态机、watchdog、命令 admission、supervision 和 Runtime lifecycle。
`rcr_cell_app` 是物理演示宿主；`rcrd` 是同一 Runtime 的 standalone host。两者不能并行写
`can0`，否则会破坏单一总线 owner 和可解释性。

Qt 不拥有 CAN 或 serial，是因为 UI event loop、用户操作和 presentation state 不应决定现场周期、
fd 生命周期或故障策略。Modbus agent 独占 tty；Qt 通过服务接口观察/commissioning，不能成为
DO0 自动闭环的第二 owner。完整模块边界见 [Code Ownership Map](../CODE_OWNERSHIP_MAP.md)。

网络/Platform 在 V1 没有 Runtime control authority。网络不可达时，本地 Runtime 不应被远端
状态覆盖；这个边界当前是 `PARTIAL / NOT_RUN` 的 Platform 验证项，而不是已经完成的跨主机
容错声明。

## 9. Key Design Decisions + Alternatives

| Decision | Chosen reason | Alternative not chosen |
|---|---|---|
| Runtime retains local authority | 网络和 Qt 可失效，但现场命令/故障语义需要本地闭合 | Platform/Qt 主控会混淆网络与现场故障 |
| `rcr_cell_app` owns physical `can0` | 一个演示进程即可组合 Runtime、CAN 和 edge policy | 与 `rcrd` 并行会产生双 CAN owner |
| CAN and Modbus stay separate | heartbeat/ACK 与 RTU Probe/confirmed 的失败行为不同 | 通用 Transport 会先抽象掉当前最重要的差异 |
| session + sequence + monotonic deadline | 拒绝旧会话、乱序和排队后过期命令 | 仅靠 UI 状态或墙钟不能防重放 |
| requested != confirmed | 请求发送不代表线圈/设备确认 | 单一“ON”状态会掩盖 transaction/设备失败 |
| immutable release + explicit rollback | 可核对 release identity，并切回明确 last-known-good | 原地覆盖使版本和恢复状态不可审计 |

Runtime 的精确 CLI、线程和 fault contract 见 [RCRD Contract](../RCRD_CONTRACT.md)。

## 10. Commissioning Flow

1. 确认 release manifest、服务、kernel/device 和总线 ownership；必要时先做只读 preflight。
2. 启动演示拓扑时，停止 standalone `rcrd`，由 `rcr_cell_app` 作为 `can0` 唯一 owner。
3. 工程师通过 Qt/CEL1 查看 Runtime、node、`POSITION_REACHED`、CellReady 和 DO0
   requested/confirmed。
4. Runtime 只在 Active、interlock ready、session/sequence 合法且 deadline 未过期时准入普通输出。
5. STM32 的 PA0 状态进入 CAN NodeStatus；mapper 在 Orange Pi 根据 snapshot 更新 CellReady，
   再通过 localhost agent 请求 MR0 DO0。
6. 所有 commissioning 结果按 evidence 等级记录；缺项保持 `NOT_RUN`。

这是参考流程，不代表一个已经关闭的现场 FAT/SAT。

## 11. Failure / Recovery

| Failure | Expected handling | Recovery boundary |
|---|---|---|
| stale/session mismatch/expired command | admission 拒绝或 ACK 不确认；旧命令不自动重放 | 新会话、有效 deadline、显式恢复 |
| CAN heartbeat/status loss | supervision 进入既有 CommLoss/Hold/Fault 语义 | 节点恢复、自检、新 session、显式操作 |
| RS-485/agent failure | Cell I/O `OFFLINE/TIMEOUT`；Qt 仍可诊断 | 显式 Probe；不重放旧 DO0 |
| bad release/config healthcheck | 停止升级并切回明确 last-known-good | rollback 后再次 healthcheck |
| missing observation source | 报告 `UNKNOWN/UNAVAILABLE` | 不伪造 healthy 或 physical state |

incident 的事实/假设/实验/恢复结构见 [incident records](../incidents/)。

## 12. Verification / Acceptance

| Layer | Current evidence | Non-claim |
|---|---|---|
| Local release candidate | LD8 baseline `b31296f` 的 fresh build/CTest、diagnostics、Operations loopback、static checks | 不等于 Orange Pi 或 physical acceptance |
| Runtime command semantics | unit/integration、vcan/simulator、incident drills | 不等于 physical lease/CommLoss matrix |
| Deployment / rollback | 临时 prefix、fake systemd、localhost fixture | 不等于 board boot、掉电或文件系统验收 |
| Physical CAN smoke | dirty-tree STM32/CAN evidence | 不等于 clean hardware acceptance |
| Closed-loop cell | checklist 有部分采集 | 整表仍 `OPEN / DEFERRED` |

权威入口为 [Acceptance Report](../LOCAL_SYSTEMS_ENGINEERING_ACCEPTANCE_REPORT.md)、
[Traceability Matrix](../REQUIREMENTS_TRACEABILITY_MATRIX.md) 和
[Evidence Index](../../evidence/README.md)。

## 13. Deployment / Rollback

release 使用 manifest/hash 标识，安装到不可变 release 路径；rollback 只切换到显式存在的
last-known-good，而不是覆盖当前安装。health、bundle 和 rollback 合同见
[Orange Pi Bring-up](../ORANGE_PI_BRINGUP.md) 与 [deployment assets](../../deploy/orangepi/README.md)。

## 14. Operations / Handover

交接时应给出：目标 release identity、服务 topology、CAN/tty owner、health 字段含义、evidence
等级、已知 incident、rollback 入口，以及“何时必须停止并升级”的条件。诊断层只能汇总来源和
availability，不能复制 Runtime state authority。

## 15. Evidence

- 证据分类与目录： [Evidence Index](../../evidence/README.md)
- LD8 本机验收： [Local Systems Engineering Acceptance Report](../LOCAL_SYSTEMS_ENGINEERING_ACCEPTANCE_REPORT.md)
- 需求到证据： [Requirements / Verification Traceability Matrix](../REQUIREMENTS_TRACEABILITY_MATRIX.md)
- 物理闭环缺项： [Closed-Loop Portfolio Freeze Gate](../plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)
- 当前项目状态： [plans index](../plans/README.md)

## 16. Known Gaps / Non-claims

- Closed-loop physical acceptance 未关闭；缺少项不能用软件结果补齐。
- 不声称 RS-485 拔线恢复、完整 physical CAN fault matrix、Orange Pi clean boot lifecycle、
  Platform 跨主机故障容错或掉电 rollback 已验收。
- 不声称 SG90 是工业伺服、PA0 是位置闭环反馈、软件 E-stop 是功能安全，或普通 Linux 是硬实时。
- 不扩展为 EtherCAT、ROS 2、PREEMPT_RT、新总线、新 UI 或第九个仓库。
