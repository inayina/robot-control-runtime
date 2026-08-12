# 最小硬件与可选扩展

## 1. 当前决定

P1 V1 使用 ThinkPad 和已选定的 Orange Pi 4 Pro 4GB。面向 Raspberry Pi 40-pin 的
RS-485/CAN HAT 已在途，但在 P1 保持断开；准确 SKU、芯片和 pinout 在 P2-G0 前仍为未知。
“Raspberry Pi HAT”不等于 Orange Pi overlay/GPIO 兼容。后续权威顺序见
[V1 收口 → BSP/Physical CAN → 真总线 Runtime](plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md)。

```text
ThinkPad ══ Wi-Fi / 管理 LAN ══ Orange Pi 4 Pro 4GB
    │                                 │
    ├─ vcan0 + rcrd（完整软件链）      ├─ 已测：SSH / 构建 / unit / ARM 矩阵
    └─ EtherCAT NIC Gate（有线口）     ├─ stock 默认：无 CONFIG_CAN，rcrd 非冷启动常驻
                                       └─ 可选 can1：已跑过 vcan0 + rcrd（非 can0，非 B4）
```

这套拓扑能够练习 SSH、ARM Linux、systemd 安装合同、POSIX 调度与 benchmark。ThinkPad
上完整验证 SocketCAN/`vcan`。Orange Pi **不是**从没跑过 `rcrd`：可选 can1 内核上已经
跑过 `vcan0 + rcrd` 软件链（见 [ORANGE_PI_CONFIG_CAN_PLAN.md](ORANGE_PI_CONFIG_CAN_PLAN.md)）。
不能写成的是另一件事：默认 stock 仍无 CAN，can1 也不是冷启动常驻，**不能**用板上
`rcrd` 冒充 V1 双端闭环或物理 `can0`。4 Pro 板载网口与 Wi-Fi 不改变“物理 CAN 未测”
的边界。

选型时的预期规格是 Allwinner A733、4GB LPDDR5、板载千兆以太网、Wi-Fi 6 和
5V/3A Type-C 供电。这里只记录采购基线；准确板卡版本、内存、镜像、内核、设备树和
接口驱动必须在实物上观察，不能预填为 PASS。

## 2. Orange Pi 到货 bring-up

按以下顺序记录证据：

1. 板卡型号、内存版本、电源与启动介质；
2. OS 镜像来源、内核、架构和编译器；
3. SSH 密钥登录、时间同步、网络地址和主机名；
4. CPU governor、频率、温度与空闲负载；
5. CMake 构建、全部 CTest 目标和 benchmark；
6. `vcan` 内核模块与 SocketCAN 权限；
7. systemd 服务的启动、SIGTERM、失败重启和日志。

同时记录 `/proc/device-tree/model`、CPU 拓扑、online CPU、频率策略、以太网 PHY/驱动、
Wi-Fi 接口和内核配置来源。不要在板卡到货前猜测 SPI pin、设备树 overlay、CAN 接口或
驱动版本，因为 V1 不使用它们。

## 3. 现有 MCU 的处理

| 板卡 | 当前处理 | 原因 |
|---|---|---|
| ESP32-S3-DevKitC-1-N16R8 | V1 不接；V1.1 可用板载 USB | 无采购即可练节点 watchdog、重启与故障注入 |
| STM32F103C8T6 Blue Pill | V1 停放；阶段 7 可选 | 只用于经 Gate 批准的物理 CAN 双位置舵机实验，不回到 MCU 电机闭环主线 |
| STM32F411 | 从本仓架构移除 | FreeRTOS/PID/Encoder/PWM 已在其他仓覆盖 |

ESP32 USB 实验不直接连接机器人执行器，不声称安全控制，也不要求 Wi-Fi。

## 4. 何时值得做物理 CAN

只有需要回答下列问题时才购买：

- 选定 Linux CAN 接口的驱动、USB 或 SPI 路径、中断与错误恢复是否可靠；
- 真实总线负载、波形、端接、错误计数和断线恢复如何；
- Linux 到 MCU 的端到端时延与 `vcan` 差异多大。

此时的最小拓扑是一个 Linux CAN 接口加一个 MCU 收发器：

```text
Orange Pi 4 Pro
  └─ explicit Linux CAN interface + transceiver
           ║ 120Ω ══ twisted pair ══ 120Ω ║
                              MCU 3.3 V transceiver
                                      └─ ESP32-S3 或 F103（二选一）
```

断电后 CANH-CANL 应接近 60 Ω；这只是端接检查，不证明信号质量。

最低风险基线仍是“Orange Pi 主控 ↔ ESP32 分布式 I/O/诊断节点”：ESP32 周期上报
heartbeat、boot counter 和 fault，普通输出命令只驱动低功耗 LED。若阶段 7 最终明确
选择 physical CAN，当前候选扩展改为 STM32F103 + SN65HVD230 + 无负载 SG90：只把 CAN V1
的 output bit 0 映射成两个固定 PWM 脉宽，用可见动作和逻辑分析仪波形验证完整链路，
不做连续角度、位置闭环或机械负载。若 PWM-only、供电或 CAN-only Gate 失败，则停在无
执行器/LED 基线。

完整 BOM、接线、命令租约、故障矩阵、证据要求和停止条件见
[STM32F103 CAN + SG90 双位置实验设计](archive/STM32_CAN_SG90_EXPERIMENT.md)。该文档目前是
Proposed，不表示硬件已经采购、固件已经实现或实物测试已经通过。

## 5. EtherCAT 对应的具身机器人场景

```text
Wi-Fi：Internet / management
        │
ThinkPad P14s Gen 6
        └─ onboard Intel e1000e NIC ── documented EtherCAT I/O SubDevice
                                          ├─ digital input
                                          └─ ordinary digital output
```

- ThinkPad 的板载有线 NIC 专用于首轮 EtherCAT，不能同时承担普通 LAN；管理连接走 Wi-Fi。
- Orange Pi 4 Pro 有板载千兆网口，但 P3 仍只负责 Runtime、SSH、systemd、`vcan` 和 ARM
  benchmark。ThinkPad 的 SOEM Gate 关闭后，可以在板上重复 ARM 主站对照；届时板载
  千兆网口必须独占，SSH、日志和普通 Modbus TCP 流量走 Wi-Fi。两块主机的内核、NIC、
  驱动和周期结果分别保存，不能混成一组结论。
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

- Modbus TCP：Orange Pi client 通过现有管理网络访问 ThinkPad reference server，模拟
  PLC、远程 I/O、仪表或驱动器的寄存器，无新增采购；若板载网口正在做 EtherCAT，
  Modbus TCP 走 Wi-Fi，不进入 EtherCAT 接口。
- Modbus RTU 协议阶段：Linux PTY 对模拟串口，无新增采购。
- Modbus RTU 实物阶段：Orange Pi USB-RS485 adapter ↔ ESP32-S3 3.3 V RS-485
  transceiver。只有需要半双工、电气层和时序证据时采购。

CAN 面向机器人内部的小报文、事件驱动和多节点总线；Modbus 面向工业设备的 client
轮询、寄存器配置和低频状态。二者不是互相替代关系，也不要求同时装在台架上。详细
顺序见 [开发路线](plans/DEVELOPMENT_ROADMAP.md)。

## 7. CAN 接口为什么不随板卡一起预购

- Orange Pi 4 Pro 官方 40-pin 功能列表只明确列出 GPIO、UART、I2C、SPI 和 PWM，未声明
  CAN；因此不能仅凭 SoC 或相似板卡资料假设存在可直接启用的 `can0`。
- 最短的物理 CAN 路径是选择有明确 Linux/SocketCAN 驱动（例如明确声明 `gs_usb`）的
  USB-CAN；SPI MCP2515 则更适合作为后续设备树、pinmux、中断和驱动实验。两条路径解决
  的学习问题不同，具体型号出现前不抽象成同一个硬件结论。
- MCP2515 是 SPI CAN 控制器，TJA1050 是 5 V CAN 收发器；Orange Pi 自身没有因此
  获得“直接 3.3 V 安全兼容”的保证。
- 市售组合板的 SPI 电平、晶振频率、`INT` 电平、端接和稳压连接不完全一致，必须
  看具体原理图，不能只看芯片名称。
- TJA1050 的总线收发能力本身可用，但廉价 5 V 模块常让 3.3 V 主控接口验证更麻烦。
- 对本项目而言，V1 使用 `vcan` 已能回答软件问题；现在买板不会改善当前学习闭环。

如果以后已有明确模块型号且原理图证明 SPI/INT 对 Orange Pi 3.3 V 兼容，可以使用，
无需因为板卡已经购买而强行选择某种 CAN 接口。

板卡资料来源：[Orange Pi 4 Pro 产品页](https://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/details/Orange-Pi-4-Pro.html)、
[Orange Pi 4 Pro 用户手册](https://orangepi.net/wp-content/uploads/2026/01/OrangePi_4_Pro_A733_User-Manual_v1.4.pdf)。

## 8. 安全边界

V1 没有电机、功率输出和硬件急停。Runtime 中的 `EStop` 与 `interlock_ready` 用来
学习锁存、拒绝和恢复流程，不是功能安全实现。以后若控制有伤害风险的设备，必须
另行设计独立硬件安全回路，并重新做风险评估；不能沿用软件模拟结论。
