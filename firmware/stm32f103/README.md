# STM32F103 暂停实验

目标硬件：已有 STM32F103C8T6 Blue Pill。状态：不属于 V1，也没有固件工程。

该板不再承担“Safety Controller”角色。普通 Blue Pill、自研固件和 GPIO 接线不能据此
宣称认证急停或功能安全。FreeRTOS、编码器、PID、PWM 等经验已经在其他仓库覆盖，本仓
不重复实现。

以后只有出现明确问题才启用，例如：

- 比较裸机中断与 Linux 用户态事件时延；
- 在物理 CAN 阶段作为 ESP32 的替代对端；
- 研究 bxCAN error state、bus-off 与 watchdog 恢复。

立项时需要单独冻结调试器、工具链、CAN 收发器、电气保护、引脚、启动默认状态和
验收矩阵。它不能成为 Orange Pi Runtime V1 的前置条件。
