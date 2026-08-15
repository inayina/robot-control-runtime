# 计划入口

本目录不再同时维护多份“当前计划”。任何时候最多只有一份执行 Gate；其余文件只提供后续
候选或长期顺序。

| 文档 | 角色 | 是否决定当前任务 |
|---|---|---|
| [PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md](PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md) | **Current Gate**：PC Qt commissioning + Orange Pi RTU 主站 | 是 |
| [REMOTE_WORKBENCH_BOUNDARY_GATE.md](REMOTE_WORKBENCH_BOUNDARY_GATE.md) | 已关闭：Remote loopback 应用边界 | 否 |
| [MODBUS_IO_MOCK_GATE.md](MODBUS_IO_MOCK_GATE.md) | 已关闭：Modbus I/O Mock / pre-hardware | 否 |
| [PC_ARM_DEVICE_CONVERGENCE_PLAN.md](PC_ARM_DEVICE_CONVERGENCE_PLAN.md) | PC→ARM→Device 长期收敛参考 | 否 |
| [PORTFOLIO_V1_RELEASE_PLAN.md](PORTFOLIO_V1_RELEASE_PLAN.md) | 未关闭的 clean 发布候选 | 否 |
| [V1_PHYSICAL_CAN_EXECUTION_PLAN.md](V1_PHYSICAL_CAN_EXECUTION_PLAN.md) | physical CAN 候选与剩余停止线 | 否 |
| [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) | EtherCAT、RT 等长期顺序参考 | 否 |

Workbench 合同见 [workbench/README.md](../workbench/README.md) 与
[workbench/GATES.md](../workbench/GATES.md)。本 Gate 不启动 EtherCAT、ROS 2、UDP
Runtime remote、A2 或 Direct CAN。

全仓文档地图回到 [docs/README.md](../README.md)。
