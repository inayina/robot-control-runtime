# Modbus I/O Mock / Pre-hardware Gate

状态：**Closed（local/dirty verification；下一 Gate 已另选 Remote Workbench Boundary）**
授权日期：2026-08-13  
目标设备候选：Amsamotion `MR0-IOR08`，4 DI + 4 relay DO，24 V，RS-485 / Modbus RTU  
证据等级：`MOCK / NO PHYSICAL RS485`

## 1. 为什么现在做

本 Gate 在硬件和正式手册到手前，先验证 Workbench 的 commissioning 用例边界：Qt 只发请求
和展示状态，Controller 编排用例，纯 C++ Mock profile 决定扫描、DI、DO 和失败行为。它不把
Modbus 接进 Runtime Core，也不把现有 Modbus TCP 实验冒充 RTU。

原 [V1 发布 Gate](PORTFOLIO_V1_RELEASE_PLAN.md) 自 2026-08-13 起改为 Deferred；其中尚未
关闭的 clean ThinkPad/Orange Pi/release evidence 保留，完成本 Gate 后再重新评审，不因顺序
调整而视为通过。

## 2. 本 Gate 的唯一实现链

```text
Qt Modbus I/O page
        │ signal / slot
        ▼
WorkbenchController
        ▼
MockModbusIoProfile
```

- `MainWindow` 只显示 snapshot/reply 并发出请求；不扫描串口、不做 timeout/retry/CRC。
- `WorkbenchController` 校验 UI 用例、发布 snapshot/reply；Mock completion 不阻塞 GUI。
- `MockModbusIoProfile` 是单线程、显式输入、确定性的纯 C++ profile；不建 worker，不读墙钟，
  不拥有 Qt、RuntimeDaemon、SocketCAN 或串口。
- 当前只有 Mock backend，不建立 `IModbusService`、`ITransport` 或 `IDevice`。

## 3. M0-M3 退出条件

### M0：authority 与边界

- 本文成为唯一 Current Gate；V1 Release 和 physical CAN 计划都标为非 Current。
- UI、日志和文档统一写 `MOCK / NO PHYSICAL RS485`。
- 不填写未经 MR0-IOR08 官方手册确认的寄存器、功能码或默认站号。

### M1：headless Mock

- typed snapshot 表达 backend、scan/device state、4 DI、4 DO requested/confirmed、last update/error；
- scan 覆盖 UNKNOWN/SCANNING/ONLINE/TIMEOUT/ERROR/recovery；
- DI 只能通过显式 Mock injection 改变；
- DO 覆盖 success/timeout/exception/rejected，失败不得改变 confirmed state；
- invalid channel 和 All OFF 有自动测试；Qt OFF 构建不查找 Qt/SerialBus/SerialPort。

### M2：Qt 页面

- 新增 `Modbus I/O (MOCK)` 页面，包含 Connection、Slave Scan、DI Monitor、DO Control；
- 页面固定展示 `MOCK / NO PHYSICAL RS485`、`NOT CONNECTED` 和 `Modbus RTU (planned)`；
- checkbox/button 只进入 Controller，请求状态与确认状态分开显示；
- QtTest 验证 signal/slot、Mock 标签、scan、DI injection、DO success/failure；不以 screenshot 验收。

### M3：验证与收口

- fresh Qt OFF/ON build 和完整 CTest 通过；vcan 相关 Skip 单独记录，不计 Modbus PASS；
- Workbench README、GATES、知识库和模块卡与代码一致；
- 明确列出 physical RS-485、真实 RTU、MR0-IOR08 register map、真实 DI/relay DO 均未实现；
- 形成一次可审计提交前保持 local/dirty 表述。

## 4. 关闭记录（2026-08-13）

| Milestone | 结果 | 证据边界 |
|---|---|---|
| M0 authority 与边界 | pass | `MOCK / NO PHYSICAL RS485` 保持；无 Serial/RTU 实现 |
| M1 headless Mock | pass | scan、DI、DO、四种 reply、ERROR/recovery、All OFF、invalid channel 均有自动测试 |
| M2 Qt presentation | pass | signal/slot 与页面状态由 offscreen QtTest 覆盖；Qt 不拥有串口或现场状态机 |
| M3 fresh verification | pass | Qt OFF 25/25；Qt ON 26/26；宿主机 `vcan0` 复跑无 skip |

验证基线为 `0a0e95064e39d966b9eda95ba59925086011c8fd`，关闭测试与文档仍未提交，故
`git_dirty=true`。这证明当前本地实现满足 Mock Gate，不是 clean release evidence，也不证明
physical RS-485。摘要见
[`evidence/portfolio/modbus_io_mock_gate_20260813.md`](../../evidence/portfolio/modbus_io_mock_gate_20260813.md)。

关闭后不自动恢复 V1 Release、physical CAN、physical RS-485 或 EtherCAT。
下一 Gate 已由用户选择为
[`REMOTE_WORKBENCH_BOUNDARY_GATE.md`](REMOTE_WORKBENCH_BOUNDARY_GATE.md)；比较记录仍见
[`SYSTEM_CONVERGENCE_AUDIT.md`](../SYSTEM_CONVERGENCE_AUDIT.md) 的 `NEXT_GATE_REVIEW`。

## 5. 当前 HAT 与设备树结论

现有硬件已确认是 Waveshare **普通版** `RS485 CAN HAT`，不是 `(B)` 版。它的 **CAN 侧**
已经在 `can2` 内核上用 Orange Pi 专用 overlay 描述 MCP2515：SPI3、PD23 interrupt、12 MHz
oscillator；`can0` probe、与 STM32F103 的双向 CAN V1、PC13 输出、SG90 无负载双位置目视
动作和专用仲裁诊断均已实际运行。证据仍是 dirty-tree partial hardware evidence，不等于 clean
hardware acceptance，详见
[`evidence/stm32f103_can/README.md`](../../evidence/stm32f103_can/README.md)。该 CAN overlay 只属于
MCP2515，不能单独证明同一块 HAT 的 RS-485 UART。

普通版的 RS-485 路径已经确定为 SoC UART + SP3485，出厂默认硬件自动收发，RSE 是可选控制；
不再把 SC16IS752 `(B)` 版列为当前硬件候选。2026-08-13 已在可回滚的 can2 启动项中独立加入
`uart7` overlay：U-Boot 同时成功应用 `mcp2515-can0` 与 `uart7`，live DT 的
`uart@7080000/status=okay`，实际节点为 `/dev/ttyS7`，绑定 `uart-ng`，没有进程、console 或 getty
占用；重启后 `can0` 仍为 UP / ERROR-ACTIVE / 500 kbit/s。证据见
[`evidence/orangepi_uart7/20260813T113157Z/`](../../evidence/orangepi_uart7/20260813T113157Z/README.md)。

真实 RS-485 Gate 开始前仍需确认：

- RSE 跳帽/控制方式、3.3 V 逻辑兼容性、RS-485 终端/偏置和 A/B/GND 接线；
- 到货设备铭牌与 MR0-IOR08 手册修订是否一致；
- 正式 RTU backend、事务 timeout、写后确认和故障恢复合同。

若使用 USB-RS485 adapter，通常不改设备树；Waveshare 普通版 HAT 的 RS-485 走 SoC UART +
SP3485，出厂默认硬件自动收发，RSE 是可选控制，因此通常只需启用物理 pin 8/10 对应的正确
Orange Pi UART/pinctrl，不能照抄 Raspberry Pi `serial0`。当前 can2 实测需要单独启用 `uart7`；
它与 `mcp2515-can0` 并列加载，没有修改 MCP2515 overlay 本身。其他 kernel flavor 不能继承这份
结论，仍须读取各自运行 DT 和串口占用。

普通版厂商资料：<https://www.waveshare.com/wiki/RS485_CAN_HAT>。当前库存型号由用户确认；
CAN 侧参数和已运行行为由仓库 overlay/evidence 约束，RS-485 侧是否可用仍须单独验证。

## 6. 后续 physical Gate（本 Gate 不执行）

```text
P0 Power     24 V、接线、A/B/GND、终端/偏置检查
P1 Serial    Linux 识别实际 UART 或 USB-RS485
P2 RTU       按官方手册读取一个合法、安全的响应
P3 DI        24 V/button → DI → Modbus → Qt
P4 DO        Qt → Modbus → relay response/必要回读
P5 Fault     断开 A/B → timeout/offline
P6 Recovery  恢复接线 → online，不重放旧请求
P7 Evidence  trace + JSON/CSV + wiring/photo/video
```

到 P2 前再比较 Qt SerialBus、libmodbus 和小型 POSIX RTU 实现；本 Gate 不选型。

## 7. Stop rules

- 为 Modbus 修改 RuntimeDaemon、CAN V1 或 STM32 固件；
- MainWindow 出现串口循环、CRC、timeout/retry 或设备状态判定；
- 引入 QtSerialPort/QtSerialBus/libmodbus；
- 建立通用 Transport/device plugin；
- 写死未核实的 MR0-IOR08 地址或功能码；
- 修改现有 CAN overlay 来“顺便启用”RS-485；
- 把 Mock、PTY、串口枚举或继电器声音写成完整 physical acceptance。
