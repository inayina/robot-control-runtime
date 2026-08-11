# ADR-001：F411 无 CAN 时的传输面划分

状态：Superseded by [ADR-002](../ADR-002-minimal-linux-runtime.md)
历史日期：2026-08-01

## 历史背景

早期方案错误地假设 STM32F411 可以直接作为 CAN 节点。确认该型号没有片上 CAN 后，
曾决定让 F411 通过 UART 承担电机执行、F103 通过 CAN 承担安全状态，并把 F411 外挂
MCP2515 延后。

## 为什么不再采用

进一步审查发现，电机闭环、FreeRTOS 和 MCU 通信已经在其他仓库覆盖；继续维护 UART
Actuator + CAN Safety 双传输会稀释本仓的 Linux Runtime 学习目标。因此当前架构完全
移除 F411 和电机链，以 ADR-002 的 Orange Pi + vcan 最小实现为准。

本文件只保留“不得假设 F411 有片上 CAN”这一硬件教训，不再作为实施依据。
