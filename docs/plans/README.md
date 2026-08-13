# 计划入口

本目录不再同时维护多份“当前计划”。任何时候最多只有一份执行 Gate；其余文件只提供后续
候选或长期顺序。2026-08-13 Remote Workbench Boundary Gate 已关闭，**当前没有 Active
implementation Gate，下一 Gate 未选择**。

| 文档 | 角色 | 是否决定当前任务 |
|---|---|---|
| [REMOTE_WORKBENCH_BOUNDARY_GATE.md](REMOTE_WORKBENCH_BOUNDARY_GATE.md) | 最近关闭的 Gate：Remote loopback 应用边界 | 否；只保存关闭记录 |
| [MODBUS_IO_MOCK_GATE.md](MODBUS_IO_MOCK_GATE.md) | 已关闭：Modbus I/O Mock / pre-hardware | 否；只保存关闭记录 |
| [PC_ARM_DEVICE_CONVERGENCE_PLAN.md](PC_ARM_DEVICE_CONVERGENCE_PLAN.md) | 用户提出的 PC→ARM→Device 长期收敛参考 | 否；不是 Current Gate |
| [PORTFOLIO_V1_RELEASE_PLAN.md](PORTFOLIO_V1_RELEASE_PLAN.md) | 未关闭的 clean 发布候选 | 否；不是 Current Gate |
| [V1_PHYSICAL_CAN_EXECUTION_PLAN.md](V1_PHYSICAL_CAN_EXECUTION_PLAN.md) | 已被单独授权部分执行的 physical CAN 候选阶段与剩余停止线 | 否；不是 Current Gate |
| [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) | EtherCAT、Modbus、RT 等长期顺序参考 | 否 |

旧 daemon / ThinkPad / Orange Pi 的 P1–P3 工作包已经完成其计划职责，只作为历史证据地图
保存在 [archive/P1_P3_EXECUTION_PLAN.md](../archive/P1_P3_EXECUTION_PLAN.md)。旧编号不能
覆盖当前 Gate，也不能排下一周任务。

Workbench 有自己的能力合同和局部 Gate，见 [workbench/README.md](../workbench/README.md)。
当前不推进新实现；A2、实物执行器、真实 RS-485、物理 PC–ARM、UDP、EtherCAT 和 Direct CAN
都未获准启动。

下一 Gate 评审见 [System Convergence Audit](../SYSTEM_CONVERGENCE_AUDIT.md#7-next_gate_review)：
physical RS-485、物理 Remote、V1 clean/physical CAN 剩余验收和 EtherCAT independent
experiment 都只是候选。已有硬件、dirty physical 结果或长期架构图不自动决定下一任务。

全仓文档地图回到 [docs/README.md](../README.md)。职责分区见
[FIVE_LAYERS_ONE_PLANE.md](../FIVE_LAYERS_ONE_PLANE.md)，它不是排期文件。
