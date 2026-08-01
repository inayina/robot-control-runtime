# 最小硬件与可选扩展

## 1. 当前决定

V1 只使用 ThinkPad 和已经下单的 Orange Pi Zero 3W，不新增实验硬件采购。

```text
ThinkPad ══ Ethernet/Wi-Fi ══ Orange Pi Zero 3W
                                  │
                               vcan0
                                  │
                           software node simulator
```

这套拓扑能够完整练习 SSH、ARM Linux、systemd、POSIX 调度、SocketCAN、epoll、故障
恢复和 benchmark。它不能验证电气层、端接、波形、总线错误或硬件安全。

## 2. Orange Pi 到货 bring-up

按以下顺序记录证据：

1. 板卡型号、内存版本、电源与启动介质；
2. OS 镜像来源、内核、架构和编译器；
3. SSH 密钥登录、时间同步、网络地址和主机名；
4. CPU governor、频率、温度与空闲负载；
5. CMake 构建、10 个测试目标和 benchmark；
6. `vcan` 内核模块与 SocketCAN 权限；
7. systemd 服务的启动、SIGTERM、失败重启和日志。

不要在板卡到货前猜测 SPI pin、设备树 overlay 或内核驱动版本，因为 V1 不使用它们。

## 3. 现有 MCU 的处理

| 板卡 | 当前处理 | 原因 |
|---|---|---|
| ESP32-S3-DevKitC-1-N16R8 | V1 不接；V1.1 可用板载 USB | 无采购即可练节点 watchdog、重启与故障注入 |
| STM32F103C8T6 Blue Pill | 停放 | 需要额外调试/收发硬件，且会重复 MCU 基础工作 |
| STM32F411 | 从本仓架构移除 | FreeRTOS/PID/Encoder/PWM 已在其他仓覆盖 |

ESP32 USB 实验不直接连接机器人执行器，不声称安全控制，也不要求 Wi-Fi。

## 4. 何时值得做物理 CAN

只有需要回答下列问题时才购买：

- Orange Pi 的实际 CAN 驱动、SPI 中断和错误恢复是否可靠；
- 真实总线负载、波形、端接、错误计数和断线恢复如何；
- Linux 到 MCU 的端到端时延与 `vcan` 差异多大。

此时的最小拓扑是一个 Linux CAN 接口加一个 MCU 收发器：

```text
Orange Pi
  └─ CAN controller + 3.3 V transceiver
           ║ 120Ω ══ twisted pair ══ 120Ω ║
                              MCU 3.3 V transceiver
                                      └─ ESP32-S3 或 F103（二选一）
```

断电后 CANH-CANL 应接近 60 Ω；这只是端接检查，不证明信号质量。

首个用途不是电机控制，而是“Orange Pi 主控 ↔ ESP32 分布式 I/O/诊断节点”：ESP32
周期上报 heartbeat、boot counter 和 fault，普通输出命令驱动板载或外接低功耗 LED。
这能验证 CAN 的总线与恢复行为，同时避免重复已有电机闭环项目。

## 5. EtherCAT 对应的具身机器人场景

```text
Wi-Fi：Internet / management
        │
ThinkPad P14s Gen 6
        └─ onboard Intel e1000e NIC ── documented EtherCAT I/O SubDevice
                                          ├─ digital input
                                          └─ ordinary digital output
```

- ThinkPad 的板载有线 NIC 专用于 EtherCAT，不能同时承担普通 LAN；管理连接走 Wi-Fi。
- Orange Pi Zero 3W 没有板载有线网口，仍负责 Runtime、SSH、systemd 和 ARM benchmark，
  不为了 EtherCAT 增加 USB 网卡。USB Ethernet 可做功能试验，但额外的 USB 调度路径会
  让周期 benchmark 更难解释，当前没有引入它的必要。
- Surface Pro 6 保留 Windows，也没有原生 RJ45，必须经 USB Ethernet 或 Surface Dock；
  它可作为普通网络对端、SSH/诊断终端或外部参考服务端，但不加入本仓构建矩阵，也
  不用 WSL2/USB 网卡结果证明 Linux EtherCAT 实时性。
- 首个 SubDevice 选简单 I/O，不选伺服。需要资料完整的 ESI/PDO/状态机说明，并在购买
  前核对模块是否还需要 24 V 电源、power terminal 和 end terminal。
- 现有 ESP32-S3/F103 不直接作为 EtherCAT SubDevice；它们缺少 EtherCAT SubDevice
  Controller（ESC）。外挂 ESC/FPGA 再开发从站栈属于另一项目，不与主站学习并行展开。
- 先用 SOEM 验证扫描、状态机、PDO、working counter、掉线与恢复；再评估 IgH 和
  PREEMPT_RT。
- 当前检测到 ThinkPad 有 `enp0s31f6`，由 `e1000e` 驱动。具体 EtherCAT 周期能力仍以
  SOEM、压力负载和掉线恢复实测为准；识别到网卡不等于工业实时性能保证。
- EtherCAT 通信本身不是安全功能，普通 I/O SubDevice 不承担急停。

该场景对应具身机器人的“计算平台 ↔ 执行层/分布式 I/O”。它比 Modbus 更贴近多轴
执行链，但也必须有真实 SubDevice 才值得进入实物阶段。

## 6. Modbus 对应的硬件场景

- Modbus TCP：Orange Pi client 通过现有 LAN 访问 ThinkPad reference server，模拟
  PLC、远程 I/O、仪表或驱动器的寄存器，无新增采购。
- Modbus RTU 协议阶段：Linux PTY 对模拟串口，无新增采购。
- Modbus RTU 实物阶段：Orange Pi USB-RS485 adapter ↔ ESP32-S3 3.3 V RS-485
  transceiver。只有需要半双工、电气层和时序证据时采购。

CAN 面向机器人内部的小报文、事件驱动和多节点总线；Modbus 面向工业设备的 client
轮询、寄存器配置和低频状态。二者不是互相替代关系，也不要求同时装在台架上。详细
顺序见 [开发路线](DEVELOPMENT_ROADMAP.md)。

## 7. MCP2515 + TJA1050 5 V 模块为什么不是默认采购

- MCP2515 是 SPI CAN 控制器，TJA1050 是 5 V CAN 收发器；Orange Pi 自身没有因此
  获得“直接 3.3 V 安全兼容”的保证。
- 市售组合板的 SPI 电平、晶振频率、`INT` 电平、端接和稳压连接不完全一致，必须
  看具体原理图，不能只看芯片名称。
- TJA1050 的总线收发能力本身可用，但廉价 5 V 模块常让 3.3 V 主控接口验证更麻烦。
- 对本项目而言，V1 使用 `vcan` 已能回答软件问题；现在买板不会改善当前学习闭环。

如果以后已有明确模块型号且原理图证明 SPI/INT 对 Orange Pi 3.3 V 兼容，可以使用，
无需因为之前的推荐而强制换品牌。

## 8. 安全边界

V1 没有电机、功率输出和硬件急停。Runtime 中的 `EStop` 与 `interlock_ready` 用来
学习锁存、拒绝和恢复流程，不是功能安全实现。以后若控制有伤害风险的设备，必须
另行设计独立硬件安全回路，并重新做风险评估；不能沿用软件模拟结论。
