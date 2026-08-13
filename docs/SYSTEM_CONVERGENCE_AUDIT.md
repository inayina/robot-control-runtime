# System Convergence Audit

审计日期：2026-08-13  
审计基线：`0a0e95064e39d966b9eda95ba59925086011c8fd`，审计开始时工作树 clean，
`main == origin/main`  
收口状态：Remote Workbench Boundary Gate 已按本地 dirty 验证关闭；**下一 Gate 未选择**

## 1. 审计方法与 authority

本次结论不是从架构图推断功能，而是交叉检查：

- 范围与计划：`SPEC.md`、`docs/plans/README.md`、当前 Gate；
- 架构与 ownership：`docs/ARCHITECTURE.md`、`docs/CODE_OWNERSHIP_MAP.md`、
  `docs/FIVE_LAYERS_ONE_PLANE.md`、Workbench 合同；
- 实现：RuntimeDaemon、LinuxRuntime、NodeSupervisor、CanIoLoop、Application Adapter、
  WorkbenchController、MainWindow、Mock profiles、Qt worker、CAN codec 与 CMake；
- 验证：当前测试、近 10 个相关提交及 `evidence/` 中对应 README/manifest。

证据等级仍使用 `MOCK`、`LOOPBACK`、`VCAN`、`PHYSICAL`，不同等级不能互相升级。

## 2. 结论摘要

### 2.1 Current Gate

审计当时的 Current Gate 是
[Modbus I/O Mock / Pre-hardware Gate](plans/MODBUS_IO_MOCK_GATE.md)，随后已关闭。用户曾选择
候选 **B** 并完成
[Remote Workbench Boundary](plans/REMOTE_WORKBENCH_BOUNDARY_GATE.md)
（`LOOPBACK / NO PHYSICAL PC-ARM`，local/dirty）。该 Gate 现已关闭；**当前没有 Active
implementation Gate**。物理 RS-485、V1 clean、physical CAN 剩余项、物理 PC–ARM 与 EtherCAT
仍保持候选，须经 `NEXT_GATE_REVIEW` 并由用户明确选择。

### 2.2 当前 Git 与验证状态

- 审计起点是 clean `main`；收口测试和文档尚未提交，因此当前结果是
  `local/dirty verification`，不是 clean release evidence。
- fresh Qt OFF：25/25 CTest passed；fresh Qt ON：26/26 CTest passed。
- 两套测试均在宿主机 `vcan0` 环境复跑，SocketCAN/Workbench vcan 用例通过，无 skip；
  QtTest 以 offscreen 平台运行通过。
- 详细边界见
  [`evidence/portfolio/modbus_io_mock_gate_20260813.md`](../evidence/portfolio/modbus_io_mock_gate_20260813.md)。

### 2.3 Qt Workbench 已真实实现

- 可选 Qt6 UI，默认构建选项 OFF；`rcr` 与 `rcr_workbench` 不依赖 Qt。
- Overview、Actuator 01 Mock、Modbus I/O Mock、Tests、Diagnostics、Results 页面。
- Qt 通过 `WorkbenchController` 消费应用 DTO；CAN Health worker 使用 `QThread`，UI 不拥有
  SocketCAN fd 或 Runtime 状态。
- Modbus Mock 在 UI event loop 中确定性完成，不创建串口、worker 或现场设备连接。
- 当前 Qt 与 Runtime 同进程，未实现 crash/failure-domain isolation。

禁止声称：Qt physical Health 已跑、PC 与 Orange Pi 已远程分离、Qt 控制了实物执行器或继电器。

### 2.4 Runtime 已真实实现

```text
RuntimeDaemon
  ├─ LinuxRuntime
  │   ├─ PeriodicScheduler
  │   ├─ StateMachine / Watchdog
  │   └─ Mailbox / ACK / Trace
  ├─ NodeSupervisor
  └─ CanIoLoop
      └─ EpollReactor + eventfd/signalfd + SocketCAN
```

已经实现并有测试的范围包括：单调时钟绝对周期、可观测 `SCHED_FIFO` 降级、状态机、watchdog、
session/sequence/deadline、单笔在途 ACK、输出 lease、故障升级、有界输入队列、SocketCAN 和
有界关闭。RuntimeDaemon 是装配与生命周期 owner；NodeSupervisor 解释节点通信状态；CanIoLoop
唯一拥有真实 CAN fd。没有第二套 CAN ownership，也没有通用 Transport。

### 2.5 Physical CAN 已真实验证

在 Orange Pi 可选 can2 内核上，Waveshare 普通版 RS485 CAN HAT 的 MCP2515 `can0` 已 probe，
并与 STM32F103 完成 dirty-tree 双向 CAN V1、PC13 输出、无负载 SG90 双位置目视动作和专用
仲裁 smoke（记录 `arbitration_lost=37`）。这属于独立 partial physical evidence。

尚未完成：clean hardware acceptance、`rcrd --can can0`、Qt physical Health、断线/bus-off/IWDG
故障矩阵、PWM 波形或认证安全证明。

### 2.6 Modbus 的真实程度

当前完成的是 `MOCK / NO PHYSICAL RS485`：scan、4 DI 显式 injection、4 DO requested/confirmed、
success/timeout/exception/rejected、ERROR/recovery、All OFF 和 invalid channel。

Orange Pi can2 已启用且观察到 `/dev/ttyS7`、驱动、live DT 和无 console/getty/进程占用；这只
关闭串口软件前置项。尚无 RS-485 字节、RTU frame/CRC、MR0-IOR08 响应、真实 DI 或 relay DO。

### 2.7 TCP/UDP 与 EtherCAT

- 本仓 Runtime/Workbench 没有 Remote Qt TCP/UDP endpoint、client 或应用协议。已有 Modbus TCP
  代码位于独立 `experiments/modbus_tcp/`，不能当成远程 Workbench。
- EtherCAT 当前是长期 roadmap、NIC 前置检查和独立实验材料。已有 ThinkPad 专用网卡条件与
  外部 SOEM empty-scan 观察，没有真实 SubDevice、OP、PDO、Working Counter 验收，也没有
  Runtime integration；它不是当前 Gate。

## 3. 当前 ownership graph

```text
MainWindow (presentation only)
        │ signal / slot
WorkbenchController (use-case orchestration)
        ├─ RuntimeApplicationAdapter ──read-only──> RuntimeDaemon
        ├─ TestRunner / CAN Health / ResultWriter
        └─ MockModbusIoProfile (isolated, deterministic, no serial)

RuntimeDaemon
        ├─ LinuxRuntime (state/watchdog/mailbox/ACK/trace transaction owner)
        ├─ NodeSupervisor (heartbeat/session/restart interpretation)
        └─ CanIoLoop (epoll and sole SocketCAN fd owner)
```

## 4. Capability matrix

| 能力链 | 当前实现 | 证据等级 | 可以表述 | 禁止表述 |
|---|---|---|---|---|
| CAN → Runtime | SocketCAN、CAN V1、supervision、fault | VCAN + partial PHYSICAL | vcan 软件闭环；独立 dirty physical smoke | clean physical acceptance / 功能安全 |
| Modbus I/O → Workbench | deterministic profile + Qt page | MOCK | Mock commissioning workflow | RTU、CRC、实物 RS-485/继电器 |
| Runtime → Qt | 同进程 adapter/DTO | local test | 可选 Qt diagnostics consumer | PC–ARM remote / crash isolation |
| TCP/UDP | Modbus TCP 独立实验；无 Remote Workbench | experiment only | 做过独立 Modbus TCP 学习实验 | Remote Runtime API 已实现 |
| EtherCAT | roadmap、NIC/empty-scan 前置 | experiment/prerequisite | 理解并准备独立 I/O Gate | SubDevice OP/PDO 或 Runtime integration |

## 5. 简历边界

可以基于现有代码和证据描述：C++20 Linux Runtime、epoll/SocketCAN/fd ownership、周期调度与
可观测降级、watchdog/fault/session/ACK、vcan 故障验证、ARM 原生构建/systemd 合同、可选 Qt
commissioning consumer，以及有明确 dirty/partial 限定的 MCP2515 ↔ STM32 physical CAN 学习实验。

不能描述：硬实时、功能安全、认证急停、完整实物执行器闭环、physical RS-485/Modbus RTU、
Remote Qt TCP/UDP、EtherCAT I/O/伺服已接入、Qt 与 Runtime 已进程隔离，或多个仓库功能已经组成
完整物理机器人闭环。

## 6. 审计发现与处理

| ID | 发现 | 本轮处理 |
|---|---|---|
| A1 | scan ERROR 缺少直接恢复测试 | 已补 headless 显式新 scan 恢复测试 |
| A2 | Qt 未覆盖 exception/rejected、All OFF 和 invalid channel 的完整传播 | 已补 QtTest |
| A3 | README/Gate/测试数量与本机 vcan 状态漂移 | 已按本轮 fresh 结果收敛 |
| A4 | clean release 与多项 physical acceptance 仍未关闭 | 保持候选，不用本轮 dirty 结果冒充关闭 |

## 7. NEXT_GATE_REVIEW

| 候选 | 自然延伸 | 面试/学习收益 | 依赖与风险 | Runtime 污染风险 | 与现有证据关系 |
|---|---|---|---|---|---|
| A Physical RS-485 / Modbus RTU | 直接把 Mock 落到现场 I/O | 串口、RTU、CRC、timeout、实物 commissioning | 依赖准确手册、24 V 接线与 HAT 电气确认 | 中；必须保持独立 adapter | 填补当前最大 Mock→physical 缺口 |
| B Remote Qt ↔ Runtime TCP/UDP | 把同进程 Workbench 拆成 PC–ARM 应用边界 | framing、Qt affinity、重连、failure-domain separation | 无设备依赖；协议与线程设计范围较大 | 中；只允许薄应用 DTO/endpoint | 新增关键系统链，近期优先级可较高 |
| C1 V1 clean evidence | 不扩功能，提升可信度 | 复现、发布、证据纪律 | 需同一 clean commit 与 ARM 重跑 | 低 | 消除现有 dirty/旧基线债务 |
| C2 Physical CAN remaining acceptance | 延续已跑硬件路径 | bus-off/断线/daemon/Qt physical | 依赖安全接线和设备操作 | 低到中 | 补齐 partial physical evidence |
| D EtherCAT independent experiment | 独立高级现场总线分支 | 状态机、PDO/SDO/WKC/cycle | 依赖真实 I/O SubDevice；范围较大 | 低，首轮禁止接 Runtime | 与现有 Runtime 主线重复最少但不是近期闭环 |

建议只作为评审输入：B 已于 2026-08-13 以 loopback Gate 关闭；C1 仍有较高近期价值；A 取决于
设备手册和安全接线；D 保持独立后续实验。物理 PC–ARM / UDP / COMMAND 是 B 的后续候选，不得
由本审计自动启动。**本文不选择下一 Gate。** 用户明确选择后，才创建/激活对应 Gate。

## 8. Stop rules

- 未选择下一 Gate 前，不新增 Serial/RTU、物理 Remote TCP、UDP、EtherCAT、A2 或 Direct CAN；
- 不为 CAN/RS-485/EtherCAT 建万能 `ITransport`、plugin framework 或超级构建；
- Remote API 不序列化 Runtime 私有结构，不为演示开放未定义 lease 的运动命令；
- Modbus 必须按真实手册和电气条件实施；串口枚举不是 physical RS-485；
- EtherCAT 首轮只能是 ThinkPad 专用网卡 + simple I/O SubDevice 的独立实验，不从伺服开始；
- 每个 Gate 都必须保留线程/时间/ownership/失败/关闭合同和对应知识卡；
- `MOCK`、`LOOPBACK`、`VCAN`、`PHYSICAL` 必须分别记录。
- loopback Remote 证据不得写成物理 PC–ARM 或产品级 crash isolation。
