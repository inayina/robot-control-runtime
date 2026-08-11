# Communication Evolution：CAN、Modbus RTU 与 EtherCAT

状态：**Roadmap — RS485 / Modbus RTU 尚未实现**

更新日期：2026-08-11

本文冻结 `robot-control-runtime` 的通信演进边界。它不是“统一总线框架”设计，也不表示
三种通信已经同时接入 Runtime。每条链路必须按自己的设备合同、时间模型和证据 Gate
独立实现与验证。

## 1. 当前证据

| 链路 | 当前状态 | 可以声称 | 不能声称 |
|---|---|---|---|
| SocketCAN / CAN V1 | `vcan` 软件链、codec、simulator、daemon 已实现 | Linux CAN 软件路径可构建和测试 | physical CAN、真实 MCU 或总线电气已验证 |
| Modbus TCP | 独立 experiment 已完成 localhost 测试和 Wi-Fi 双机 demo | 使用过 MBAP/PDU 与最小互操作路径 | 已接入 Runtime 或真实工业设备 |
| RS485 / Modbus RTU | **Planned / not implemented** | 已形成开发边界与退出条件 | 串口、RS-485 电气或 RTU 设备已验证 |
| EtherCAT | 协议学习与 ThinkPad NIC Gate；无 SubDevice 闭环 | 已评审主站入口和专用 NIC 条件 | PDO 周期、WKC、DC 或伺服已验证 |

`vcan`、PTY 和 simulator 都是软件证据。它们不能替代 physical CAN、RS-485 收发器、电气
端接、噪声、bus-off 或真实 EtherCAT SubDevice 的实物证据。

## 2. 三种通信解决不同问题

| 维度 | SocketCAN / CAN | Modbus RTU over RS485 | EtherCAT |
|---|---|---|---|
| 基本语义 | frame / message based | master request / slave response | cyclic process data + mailbox |
| 数据组织 | CAN ID、payload、节点协议 | function code、coil/register、exception | PDO、SDO、ESC/状态机 |
| 典型对象 | MCU 节点、电机控制器、机器人设备 | 工业传感器、仪表、PLC、外围 actuator | 确定性多轴运动与高速 I/O |
| 时间模型 | 仲裁总线；heartbeat/deadline 由上层合同定义 | 串行轮询；response timeout 与设备扫描周期 | 固定周期；可选 Distributed Clocks 同步 |
| 主要故障 | decode reject、heartbeat loss、bus error/bus-off | CRC、地址/功能异常、timeout、malformed response、offline | WKC、状态机、link、cycle/DC 偏差 |
| 本仓角色 | 当前 Runtime 主设备路径 | 未来低频工业设备集成支线 | 独立的确定性 I/O / motion 学习轨道 |

因此不建立只有 `send(bytes)` / `receive(bytes)` 的通用 `ITransport`。这种接口会隐藏 CAN
仲裁与 heartbeat、Modbus 请求归属与寄存器语义、EtherCAT PDO 周期与 WKC/DC，反而让 timeout、
ownership 和恢复策略无法正确表达。当前继续保留窄而明确的协议/设备适配层；只有出现两个
已经存在、行为确实相同的实现后，才评审可复用代码。

## 3. RS485 / Modbus RTU 的正式定位

它用于工业设备、传感器、仪表、PLC 或低频 actuator integration，不承担机器人高频运动
闭环、硬件急停或 EtherCAT 多轴周期数据职责。Qt 也不直接拥有串口；未来仍走：

```text
Qt Workbench
  → Application / Modbus device adapter
  → Runtime-side serial owner
  → Linux serial configuration / RS485
  → Modbus RTU
  → real device or separately approved STM32 slave
```

当前 STM32F103 仍按仓库权威边界停放。只有真实设备合同、供电/接线和独立固件范围获批后，
它才可作为 RTU slave 候选；本路线图不自动启动 MCU 开发。

## 4. 小而真实的 RTU 闭环

第一版只覆盖一个 slave 和一份冻结的寄存器合同：

- 串口配置：device、baud、parity、data bits、stop bits；
- RS-485 半双工方向/适配器能力与 slave address；
- `0x03` 读 Holding Registers；
- `0x06` 写 Single Register；
- 真实需求出现后再加入 `0x10` 写 Multiple Registers；
- 显式寄存器地址、单位、缩放、字节/word order；
- CRC16 校验、exception response、错误地址和 malformed response；
- response timeout、device offline 与有界诊断计数；
- retry policy：读请求可做有界重试；写请求默认不盲目重放，必须先确认设备幂等语义和
  “请求已执行但响应丢失”的处理合同；
- Test Runner 的 Prepare / Execute / Evaluate / Cleanup 集成。

第一阶段先用 PTY 验证 codec、解析与 timeout；随后才用 USB-RS485 + 真实设备或获批的 MCU
slave 验证半双工、电气端接和真实时序。PTY PASS 只能标为 simulation，不是 physical RS485。

未来首个自动用例可以命名为：

> **Modbus Communication Health Test**

它至少记录 serial configuration、slave address、function code、寄存器范围、响应时间、CRC/
malformed/timeout 计数、重试次数、offline 判定、cleanup 与 `MOCK` / `PHYSICAL` evidence label。

## 5. 演进顺序与独立 Gate

为避免与历史 P1/P2/P3、Real-time RT0–RT7 和 EtherCAT Gate 编号冲突，通信支线使用 `C`：

```text
C1  Real CAN Hardware Validation
 ↓
C2  RS485 / Modbus RTU Bring-up（PTY → physical Gate）
 ↓
C3  Modbus Device Integration + Diagnostics + Test Runner
 ↓
C4  CAN FD Evaluation（只有带宽/载荷证据表明确有需要时）

E1+ EtherCAT Track：专用 NIC → simple I/O SubDevice → PDO/WKC → 可选 DC/servo
RT   PREEMPT_RT：保持独立可行性 Gate；不是某种通信驱动的实现步骤
```

这表示通信能力的推荐收敛顺序，不表示 EtherCAT 必须等待 C4。EtherCAT 面向不同问题，仍按
既有 NIC/SubDevice Gate 独立推进；任一时刻只打开一个有明确退出条件的实物工作包。

## 6. Workbench 接入边界

```text
CAN health case     → RuntimeApplicationAdapter / Runtime-owned CAN diagnostics
Modbus health case  → serial owner / RTU client / register diagnostics
EtherCAT test       → cyclic owner / PDO-WKC diagnostics
```

三者可以复用 Test Runner 的测试生命周期、统一 evidence label 和结果展示，但不复用伪通用的
transport 语义。Qt 只发起 use case 并显示 snapshot/result；fd、串口、周期线程和恢复策略留在
相应的 headless application/runtime 模块。

当前 CAN Health 不建立 Direct CAN session，而是只读采样 Runtime 已解码和监督后的 snapshot。
未来若需要脱离 Runtime 的 Direct CAN bring-up，必须先关闭 interface/DUT 跨进程独占 lease、
`rcrd` 冲突检测、命令 authority 和崩溃回收 Gate；它不能反向成为 CAN/Modbus/EtherCAT 的
通用 transport 抽象。

## 7. 本轮明确不实现

- Linux serial/RS485 驱动或 `QSerialPort`；
- Modbus RTU codec、client、slave 或真实寄存器表；
- Modbus 自动重试框架；
- STM32 RTU firmware 和电气接线；
- CAN FD、EtherCAT 或 PREEMPT_RT 代码；
- 跨 CAN、Modbus、EtherCAT 的 generic Transport。

长期总路线见 [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md)，Qt/Runtime 应用边界见
[DEVICE_TEST_DIAGNOSTIC_WORKBENCH_DEVELOPMENT_PLAN.md](DEVICE_TEST_DIAGNOSTIC_WORKBENCH_DEVELOPMENT_PLAN.md)。
