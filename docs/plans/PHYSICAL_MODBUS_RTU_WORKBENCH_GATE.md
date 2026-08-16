# Physical Modbus RTU → Qt Workbench Integration Gate

状态：**Prerequisite（不再扩张；Current Gate 已替换）**  
授权日期：2026-08-15  
后继 Current Gate：[Closed-Loop Portfolio Freeze](CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)  
目标设备：Amsamotion `MR0-IOR08`，4 DI + 4 relay DO，24 V，RS-485  
证据等级目标：`PHYSICAL MODBUS RTU`（commissioning GUI 在 PC；现场主站在 ARM）  
先前关闭：Mock I/O Gate、Remote Workbench Boundary（loopback）

## 1. 为什么现在做

2026-08-15 已在 Orange Pi `can2` 上用 `/dev/ttyS7` 对 HAT A/B 发出只读 RTU，站号 1、
9600 8N1 稳定返回合法 CRC（FC02/FC01/FC03/FC04）。这证明电气层和从站应答，不是 Qt
闭环，也不是工业产线形态。

本 Gate 把这条已验证的 RTU 链接到现有 Qt Device Workbench。Qt 留在 ThinkPad 方便录屏
和回归；阻塞串口事务留在拥有 UART 的 Orange Pi。网上只走有界 commissioning 请求，不
转发 raw tty，也不并入 Runtime `HELLO/HEARTBEAT/GET_STATUS` 控制面。

这不是「工业一版标准接法」。工业一版调试通常是 PC 插 USB-RS485 直连模块；量产则是
PLC/IPC 拥有现场总线、HMI 走以太网。本台架用现有 HAT 路径，演示工程站与现场主站分
开；面试时必须能说出这个差别。

## 2. 本 Gate 的唯一实现链

```text
ThinkPad Qt MainWindow
        │ signal / slot（不打开串口、不编 RTU、不阻塞）
        ▼
WorkbenchController
        │ MOCK → MockModbusIoProfile（回归，显式选择，永不静默回退）
        │ PHYSICAL → queued request
        ▼
ModbusAgentWorker（QThread）
        │ POSIX TCP client
        ▼
LAN（ThinkPad 192.168.1.8 ↔ Orange Pi 192.168.1.22）
        ▼
rcr_modbus_rtu_agent
        ▼
PhysicalModbusIoService
        ▼
POSIX termios 一次完整 RTU 事务
        ▼
/dev/ttyS7 → SP3485 → RS-485 A/B → MR0-IOR08
```

- `rcrd` / `CanIoLoop` / SocketCAN 不参与。
- 不建 `ITransport` / `FieldbusManager` / 通用 plugin。
- 不把 Modbus 消息塞进 Remote Workbench 控制面。
- 写线圈不得盲目重试；恢复后先读实际 DI/DO，再接受新命令。

## 3. 已冻结的现场合同（live probe，dirty）

| 项 | 值 | 边界 |
|---|---|---|
| 串口 | `/dev/ttyS7`，`uart-ng`，UART7 overlay | 仅 can2；stock 内核无此路径 |
| 链路 | HAT SP3485 硬件自动收发 | 未测 RSE 跳帽变体 |
| 从站 | slave id 1 | 未扫 2–247 |
| 串口参数 | 9600 8N1（8E1 也曾收到同帧，本 Gate 用 8N1） | 19200/115200 无应答 |
| 安全读 | FC02 start=0 qty=8 → 1 data byte，DI bit0–3 | 手册未入库；以本机应答为准 |
| 线圈读 | FC01 start=0 qty=8 | Probe 刷新 confirmed；失败不把 DI ONLINE 打成 TIMEOUT |
| 线圈写 | FC05 单线圈，地址 = 通道号 | 2026-08-16 live：DO0 ON `01050000ff008c3a` 回显 OK，随即 OFF `010500000000cdca`；无市电负载 |
| 禁止 | 无限重试、恢复后自动重放 DO、市电/大功率负载 | FC0F 尚未 live-verify；ALL OFF 连发 FC05 |

## 4. 里程碑

### M0：authority

- 本文不再是 Current Gate；后继见 Closed-Loop Portfolio Freeze。
- 文档写清 PC Qt ≠ 现场主站；Mock / Physical 显式选择。演示拓扑改为 Orange Pi 同进程
  Workbench + localhost agent，不扩张 Remote Workbench。

### M1：Probe → ONLINE（本轮必须先关上）

- Qt Probe → Controller → worker → agent → 真实或注入的 FC02 → Qt 显示 ONLINE。
- MainWindow 不访问 serial/TCP/CRC。
- timeout 有界；UI 保持可响应。
- 可见 `PHYSICAL MODBUS RTU` 标签。
- Mock 回归保持。

### M2：DI 轮询

- 保守周期（约 500 ms），不是控制环。localhost 合同已测。
- 真实 DI 边沿更新 Qt；Physical 禁用 Mock injection。板上边沿录屏仍属 M5。

### M3：DO requested ≠ confirmed + ALL OFF

- 2026-08-16 已在板上 live-verify FC05（DO0 ON 回显后立即 OFF）。
- 失败不把 requested 写成 confirmed；不自动重放。localhost 合同已测。

### M4：断线 / 恢复 / 关闭

- 拔 A/B → TIMEOUT/OFFLINE；UI 不卡死；重接后显式 Probe，不重放旧 DO。
- 窗口关闭时 worker/agent 连接确定性拆除。

### M5：45–90 s 录屏证据

按用户给定步骤采集；没拍到的不写 PASS。

## 5. 选型（与备选）

| 选择 | 不选 | 原因 |
|---|---|---|
| 板上 POSIX RTU + 最小 TCP agent | QtSerialBus / libmodbus / socat 转发 tty | Qt 不能进 `rcr_workbench`；libmodbus 对 4 路已验证功能码过重；字节转发会把 3.5 字符间隔丢到 Wi-Fi 上 |
| 独立 agent 端口与帧 magic | 复用 Runtime Remote 控制面 | Operator↔Runtime 与 commissioning↔现场 I/O 语义不同 |
| 第二根 QThread | 复用 CAN Health worker | Health `run()` 占满该线程 event loop |
| 无 `IModbusService` vtable | 通用 device/plugin | 两个 backend 只共享 `ModbusIoSnapshot` |

## 6. Stop rules

- 修改 `RuntimeDaemon`、CAN V1、STM32 或现有 MCP2515 overlay。
- MainWindow 打开 serial/TCP 或解析 RTU。
- Physical 失败时静默切回 Mock。
- 无限重试、忙等、自动重放 DO。
- 把 Mock、loopback agent 或串口枚举写成 physical PASS。
- EtherCAT、ROS 2、UDP telemetry、图表、多 Modbus 设备、通用总线框架。

## 7. 关闭条件

M1 可在 ThinkPad localhost + 注入 FC02 先证明 GUI/线程合同；physical PASS 仍要求
Orange Pi agent + `/dev/ttyS7` + MR0 应答。Gate 在 M5 录屏证据齐备后关闭。
