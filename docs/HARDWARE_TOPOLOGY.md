# 最小硬件与可选扩展

## 1. 当前决定

P1 V1 仍使用 ThinkPad 和 Orange Pi 4 Pro 4GB，不依赖新增硬件。2026-08-13 用户另行授权
独立 physical CAN 学习支线：Raspberry Pi 40-pin HAT 已识别为 MCP2515/12 MHz 路径，
Orange Pi 使用 can2 + SPI3/PD23 overlay 得到 `can0`，第二节点是 STM32F103 + 3.3 V
SN65HVD230。该支线不改变 [V1 发布 Gate](plans/PORTFOLIO_V1_RELEASE_PLAN.md)，未关闭项目见
[Physical CAN 候选方案](plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md)。

```text
ThinkPad ══ Wi-Fi / 管理 LAN ══ Orange Pi 4 Pro 4GB
    │                                 │
    ├─ vcan0 + rcrd（完整软件链）      ├─ 已测：SSH / 构建 / unit / ARM 矩阵
    └─ EtherCAT NIC Gate（有线口）     ├─ stock 默认：无 CONFIG_CAN，rcrd 非冷启动常驻
                                       ├─ can1：vcan0 + rcrd（非 can0，非 B4）
                                       └─ can2：SPI3 → MCP2515 → can0
                                                            ║ physical CAN
                                              SN65HVD230 ← STM32F103
                                                            ├─ PC13
                                                            └─ PA8/TIM1 → SG90
```

ThinkPad `vcan` 仍是 V1 Runtime 正式软件对照。can1 证明板上跑过 `vcan0 + rcrd`；can2
独立证明 MCP2515 `can0` 与 STM32F103 的双向协议、PC13、无负载 SG90 双位置目视动作和
专用仲裁 smoke。三者证据不能互换：默认 stock 仍无 CAN，B4 未关，can2 尚未运行
`rcrd --can can0`、Qt physical Health、PWM 波形或完整物理故障矩阵。详情见
[Orange Pi CAN 记录](ORANGE_PI_CONFIG_CAN_PLAN.md)和
[STM32 physical evidence](../evidence/stm32f103_can/README.md)。

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
Wi-Fi 接口和内核配置来源。SPI pin、overlay、CAN 接口和驱动只使用 can2 实测记录；以后
换 HAT、内核或 pinout 时必须重新识别，不能从当前组合外推。

## 3. 现有 MCU 的处理

| 板卡 | 当前处理 | 原因 |
|---|---|---|
| ESP32-S3-DevKitC-1-N16R8 | V1 不接；V1.1 可用板载 USB | 无采购即可练节点 watchdog、重启与故障注入 |
| STM32F103C8T6 Blue Pill | 独立实验已实现；V1 不依赖 | 裸机 bxCAN/CAN V1、PC13、PA8/TIM1 两档 SG90 与仲裁诊断；不回到 MCU 电机闭环主线 |
| STM32F411 | 从本仓架构移除 | FreeRTOS/PID/Encoder/PWM 已在其他仓覆盖 |

ESP32 USB 实验不直接连接机器人执行器，不声称安全控制，也不要求 Wi-Fi。

## 4. Physical CAN 当前结果与停止线

当前台架已经回答：

- MCP2515 经 SPI3/PD23 overlay 可以注册并 UP 为 SocketCAN `can0`；
- STM32F103 bxCAN + SN65HVD230 能与 Orange Pi 双向交换 CAN V1 帧；
- bit0 能驱动 PC13，并在 lease 有效时映射成无负载 SG90 两档目视动作；
- 专用诊断固件直接记录过真实仲裁失败者的 `ALST0`，没有同时产生发送错误。

此时的最小拓扑是一个 Linux CAN 接口加一个 MCU 收发器：

```text
Orange Pi 4 Pro
  └─ SPI3/PD23 → MCP2515 HAT → transceiver
           ║ 120Ω ══ twisted pair ══ 120Ω ║
                              SN65HVD230（3.3 V）
                                      └─ STM32F103
```

断电后两个末端各 120 Ω 的设计值应使 CANH-CANL 接近 60 Ω；当前没有保存这项测量，也没有
CANH/CANL 或 PA8 波形，所以不能声称信号质量、实际 PWM 脉宽或关闭时序通过。下一步若继续，
只关闭 [`firmware/stm32f103/SPEC.md`](../firmware/stm32f103/SPEC.md) 中尚未运行的 S0–S2、
断线/bus-off/IWDG 和 clean 同提交证据；不增加连续角度、位置闭环或机械负载。历史 Proposed
设计保留在 [archive/STM32_CAN_SG90_EXPERIMENT.md](archive/STM32_CAN_SG90_EXPERIMENT.md)，
不再作为当前状态 authority。

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

## 7. 当前 CAN 接口结论为什么不能外推

- Orange Pi 4 Pro 官方 40-pin 功能列表只明确列出 GPIO、UART、I2C、SPI 和 PWM，未声明
  CAN；因此不能仅凭 SoC 或相似板卡资料假设存在可直接启用的 `can0`。
- 当前选用 SPI MCP2515 是为了同时验证设备树、pinmux、中断和驱动路径；它不证明另一款
  HAT、USB-CAN 或 SoC 原生 CAN 可以复用同一配置。
- MCP2515 是 SPI CAN 控制器，TJA1050 是 5 V CAN 收发器；Orange Pi 自身没有因此
  获得“直接 3.3 V 安全兼容”的保证。
- 市售组合板的 SPI 电平、晶振频率、`INT` 电平、端接和稳压连接不完全一致，必须
  看具体原理图，不能只看芯片名称。
- TJA1050 的总线收发能力本身可用，但廉价 5 V 模块常让 3.3 V 主控接口验证更麻烦。
- V1 Runtime 的软件结论仍由 ThinkPad `vcan` 提供；can2 台架补的是 Linux 无法模拟的真实
  peer、收发器和仲裁证据，不替代 clean Runtime Gate。

以后更换模块时仍须重新证明 SPI/INT、电源和 Orange Pi 3.3 V 兼容；不能因为当前板已工作就
省略新型号的识别 Gate。

板卡资料来源：[Orange Pi 4 Pro 产品页](https://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/details/Orange-Pi-4-Pro.html)、
[Orange Pi 4 Pro 用户手册](https://orangepi.net/wp-content/uploads/2026/01/OrangePi_4_Pro_A733_User-Manual_v1.4.pdf)。

## 8. 安全边界

V1 没有电机、功率输出和硬件急停。Runtime 中的 `EStop` 与 `interlock_ready` 用来
学习锁存、拒绝和恢复流程，不是功能安全实现。以后若控制有伤害风险的设备，必须
另行设计独立硬件安全回路，并重新做风险评估；不能沿用软件模拟结论。
