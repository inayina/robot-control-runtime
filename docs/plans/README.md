# 计划入口

本目录不再同时维护多份“当前计划”。任何时候只有一份执行 Gate；其余文件只提供后续
阶段编号或长期顺序。

| 文档 | 角色 | 是否决定当前任务 |
|---|---|---|
| [MODBUS_IO_MOCK_GATE.md](MODBUS_IO_MOCK_GATE.md) | **当前唯一执行 Gate：Modbus I/O Mock / pre-hardware** | **是** |
| [PORTFOLIO_V1_RELEASE_PLAN.md](PORTFOLIO_V1_RELEASE_PLAN.md) | 未关闭的 clean 发布候选 | 否；不是 Current Gate |
| [V1_PHYSICAL_CAN_EXECUTION_PLAN.md](V1_PHYSICAL_CAN_EXECUTION_PLAN.md) | 已被单独授权部分执行的 physical CAN 候选阶段与剩余停止线 | 否；不是 Current Gate |
| [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) | EtherCAT、Modbus、RT 等长期顺序参考 | 否 |

旧 daemon / ThinkPad / Orange Pi 的 P1–P3 工作包已经完成其计划职责，只作为历史证据地图
保存在 [archive/P1_P3_EXECUTION_PLAN.md](../archive/P1_P3_EXECUTION_PLAN.md)。旧编号不能
覆盖当前 Gate，也不能排下一周任务。

Workbench 有自己的能力合同和局部 Gate，见 [workbench/README.md](../workbench/README.md)。
当前只推进 Modbus I/O Mock 页面；A2、实物执行器和真实 RS-485 仍不启动。

当前 Mock Gate 关闭后，是恢复 V1 clean 发布、关闭 physical CAN 剩余验收还是转向真实
RS-485，必须重新评审；已有 dirty-tree 双向 CAN/PC13/SG90/仲裁结果和手边 HAT 不自动决定
下一任务。

全仓文档地图回到 [docs/README.md](../README.md)。职责分区见
[FIVE_LAYERS_ONE_PLANE.md](../FIVE_LAYERS_ONE_PLANE.md)，它不是排期文件。
