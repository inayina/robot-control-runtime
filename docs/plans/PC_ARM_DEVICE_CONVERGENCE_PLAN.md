# PC → ARM Runtime → Device 收敛计划

状态：**Reference / Not Current**（§3 曾由
[REMOTE_WORKBENCH_BOUNDARY_GATE.md](REMOTE_WORKBENCH_BOUNDARY_GATE.md) 吸收并关闭；
本文仍不是排期文件）
记录日期：2026-08-13  
用途：保存用户提出的长期工作计划；任何实现仍由 `docs/plans/README.md` 指向的唯一 Current
Gate 决定。

## 1. 长期目标与边界

```text
PC Qt operator / diagnostics
             │ TCP / UDP（候选应用边界）
             ▼
Orange Pi Linux RuntimeDaemon
state / watchdog / fault / supervision
             │
       ┌─────┼──────────────┐
       │     │              │
      CAN   RS-485       EtherCAT
       │   Modbus RTU    future branch
       ▼     ▼              ▼
      MCU  Remote I/O   distributed I/O/drive
```

这是一张 **capability architecture**，不是所有链路都已部署的产品架构。项目逐 Gate 证明：

> 设备通信 → Linux Runtime → 状态监督 → PC Qt 操作/诊断端

EtherCAT 是独立高级分支，不因长期图存在就接入当前 Runtime。

## 2. 不变原则

1. 一次只有一个 Current Gate；当前 Gate 关闭后先评审，不自动启动下一项。
2. CAN、RS-485/Modbus 与 EtherCAT 各自拥有真实协议和故障语义，不建立万能 `ITransport`、
   `UniversalDevicePlugin` 或超级构建。
3. RuntimeDaemon 继续拥有 Runtime 生命周期；CanIoLoop 继续唯一拥有 CAN fd；Qt 只消费应用 DTO。
4. 本仓不重复 `robot-ops-dashboard` 的 HTTP/WebSocket/MQTT，也不搬嵌入式姊妹仓的 MCU/RTOS
   代码来制造假集成。
5. 每项证据标为 `MOCK`、`LOOPBACK`、`VCAN` 或 `PHYSICAL`，不能混写。

## 3. Remote Workbench Boundary 候选

用户已于 2026-08-13 选择、完成并关闭
[REMOTE_WORKBENCH_BOUNDARY_GATE.md](REMOTE_WORKBENCH_BOUNDARY_GATE.md)（loopback）。以下保留
设计意图；物理 PC–ARM / UDP / COMMAND 须另开 Gate。

目标不是堆 TCP/UDP 关键词，而是为 PC Qt 与 Orange Pi Runtime 的物理拆分建立薄应用边界和
failure-domain separation。

当前：

```text
Qt Workbench → RuntimeApplicationAdapter → RuntimeDaemon
```

候选：

```text
                   WorkbenchController
                           │
                 Runtime Client Contract
                    ┌──────┴──────┐
                    │             │
          LocalRuntimeAdapter  RemoteRuntimeClient
                    │             │ TCP / UDP
              RuntimeDaemon  Orange Pi endpoint
```

名称是候选，不是已冻结 API。Local mode 必须保留，不能为了两个 backend 建 plugin framework。

### 3.1 应用 DTO

先审计现有 snapshot，再提取稳定业务字段，例如 runtime/interlock/fault、device/session health、
heartbeat age、CAN/queue/drop counters 和 command result。禁止网络暴露 raw fd、pointer、mutex、
scheduler、内部队列、Linux owner 或对 Runtime private struct 做网络 `memcpy`。

### 3.2 TCP 控制面

TCP 只承载可靠、低频、请求/响应语义。第一版优先 `HELLO`、`HEARTBEAT`、`GET_STATUS`；只有
命令 session/sequence/deadline/lease 和恢复合同明确后，才评审 `COMMAND/COMMAND_REPLY`。

V1 frame 至少要有 magic、version、message type、sequence、payload length、bounded payload
和完整性检查。pure C++ parser 必须覆盖 partial frame、一次读到多个 frame、invalid magic、
unsupported version/length、oversize、CRC failure、unknown type 和 bounded RX buffer。绝不能
假设一次 `readyRead` 等于一个 packet。

### 3.3 UDP telemetry plane

UDP 只发送 Runtime 已真实拥有的 latest-wins telemetry：sequence、monotonic timestamp、runtime/
fault state、session health、CAN/queue/drop counters、heartbeat freshness。必须观察 received、gap、
duplicate、out-of-order、malformed、CRC failure、rate 和 freshness。不伪造 IMU、sonar、DVL、
camera 或水下机器人数据；也不能简化成“UDP 比 TCP 实时”。

### 3.4 Qt 线程与关闭

若网络放入 worker：socket/QTimer 必须在正确线程创建与销毁，UI 不直接操作 socket，worker
不修改 QWidget，跨线程只传 typed DTO 并使用 queued signal/slot。关闭合同必须覆盖停止输入、
关闭 socket/timer、`deleteLater`、`quit`、有界 `wait` 和对象拆除顺序。

### 3.5 先 loopback，再 physical PC–ARM

首个 integration fixture 应是 `RemoteRuntimeClient ↔ loopback runtime simulator`，提供确定性
status/heartbeat/telemetry，以及断连、停止 heartbeat/telemetry、bad CRC、可调周期等故障注入。
协议、线程、timeout/recovery 关闭后，再单开 `PHYSICAL_PC_ARM_REMOTE_GATE` 验证物理 Ethernet。
不要第一步改正式 `rcrd`，也不要同时实现 pipe、POSIX MQ、shared memory 或 Unix Domain Socket。

UI 只允许增加薄 Connection 状态，如 backend、TCP/UDP、peer、heartbeat、rate、gap、reconnect；
不扩成带地图、视频、3D 或虚构传感器的大型操作站。

## 4. Physical RS-485 / Modbus RTU 候选

它是独立设备链：Operator ↔ Runtime 的 TCP/UDP 语义不能与 Runtime/commissioning tool ↔ Field
Device 的 RTU 语义混合。Gate 开始前必须确认设备 SKU/手册修订、24 V、A/B/GND、终端/偏置、
RSE/自动收发和 3.3 V 逻辑兼容性。

选型只在开 Gate 后比较 Qt SerialBus、libmodbus 和最小 POSIX serial。实现必须定义 baud、parity、
slave id、CRC16、timeout、写后确认、断开/重连和 retry；写请求不得盲目重试。最终验证真实 DI、
relay DO 和 physical fault/recovery。`/dev/ttyS7` 存在只算软件前置，不算 RTU evidence。

## 5. EtherCAT 独立实验候选

首轮仍使用 ThinkPad 独占 Intel 有线网卡和 simple I/O SubDevice，管理流量走 Wi-Fi。目标是理解
MainDevice/SubDevice、INIT/PREOP/SAFEOP/OP、PDO/SDO、process image、Working Counter、cycle
和 link-loss recovery；IgH/SOEM 只在 Gate 开始时选。首轮不买伺服、不在 Orange Pi 起步、
不接 Runtime Core。独立实验完成后再评审只读 observation adapter 是否有真实价值。

## 6. 测试与知识库纪律

每个被选择的 Gate 必须同时形成：

- pure C++：codec/parser/state/boundary/malformed input；
- Qt：QSignalSpy、state propagation、thread affinity、shutdown、offscreen；
- integration：进程 fixture、有界 timeout、故障注入与 recovery；
- evidence：环境、commit/dirty、命令、结果、失败分类和证据等级；
- Knowledge Base：直觉、user/kernel 边界、数据链、线程/时间、ownership、失败、备选与实验。

## 7. 禁止项与激活条件

禁止为展示而引入 gRPC、ZeroMQ、DDS、shared memory、plugin framework、万能 transport、第二套
CAN fd owner、虚构 telemetry、伺服 EtherCAT 首轮或 fake physical acceptance。

计划文件存在不等于授权。激活条件是：当前没有未关闭 Gate，完成
[`SYSTEM_CONVERGENCE_AUDIT`](../SYSTEM_CONVERGENCE_AUDIT.md) 中的 `NEXT_GATE_REVIEW`，用户明确选择
一个候选，并建立包含输入输出、线程/时间、ownership、failure、验证和 stop rules 的唯一 Gate。
