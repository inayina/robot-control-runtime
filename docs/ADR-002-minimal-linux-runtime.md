# ADR-002：收敛为最小 Linux Runtime

状态：Accepted  
日期：2026-08-01

取代已归档的 [ADR-001](archive/ADR-001-f411-no-native-can.md)。全仓文档地图见
[README.md](README.md)。

2026-08-03 修订说明：最小 Runtime 与 `vcan` 决策不变；原 Orange Pi Zero 3W 已退货，
ARM 部署目标改为 Orange Pi 4 Pro 4GB。板卡更换只改变 P3 的实机环境和后续有线网口
选项，不把物理 CAN、Modbus 或 EtherCAT 提前纳入 V1。

2026-08-13 后续说明：用户单独授权并完成了一条 STM32F103 + MCP2515 physical CAN
dirty-tree smoke 支线。该事实不修改本 ADR 对 **V1 Runtime** 的范围决定；F103 的当前状态
改由根 SPEC、固件 SPEC 和 Current Gate 解释，下面“暂停”保留为 2026-08-01 的原始决策。

## 背景

原方案包含 Orange Pi、F411 电机闭环、F103 安全节点、ESP32 诊断节点、UART、CAN、
电机驱动和硬件安全链。跨六个姊妹仓审查后发现，FreeRTOS、编码器、PID、PWM、单电机
bench 和 micro-ROS 已有实践。本仓的独特求职价值应是 Linux 底层运行时与部署。

当时用户已经下单 Orange Pi，希望学习 SSH、Linux 调度和部署，同时不希望继续购买大量
硬件；具体型号随后按上述修订调整。

## 决策

1. V1 只使用 ThinkPad、Orange Pi 和 `vcan`，不新增实验硬件采购。
2. 首个端到端对端是独立 CAN 节点模拟器。
3. ESP32-S3 仅作为完成 V1 后的可选 USB 节点实验。
4. STM32F103 暂停；F411、电机和硬件安全链从本仓主线移除。
5. 不建立通用 Transport；物理 CAN 真正出现前始终直接围绕 SocketCAN 实现。
6. 软件状态机明确标注非功能安全实现。

## 结果

收益：BOM 降为零，测试可自动化，Orange Pi 部署成为明确成果，且不重复其他仓库。

取舍：V1 不提供真实 CAN 电气层、MCU 时序或硬件安全证据。这些能力只有在 Linux
端到端和部署证据完成后，按具体学习问题单独增加。

## 被否决方案

- 同时开发 F411、F103、ESP32：职责和工具链过多，学习证据被摊薄。
- 直接采购 24 V 安全 I/O 台架：对当前 Linux 底层目标投入过大，且原型不能冒充认证。
- 为 CAN/USB/Modbus 预建统一 Transport：尚无两个真实实现，属于提前抽象。
