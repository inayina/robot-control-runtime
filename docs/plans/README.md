# 计划入口

本目录不再同时维护多份“当前计划”。任何时候只有一份执行 Gate；其余文件只提供后续
阶段编号或长期顺序。

| 文档 | 角色 | 是否决定当前任务 |
|---|---|---|
| [PORTFOLIO_V1_RELEASE_PLAN.md](PORTFOLIO_V1_RELEASE_PLAN.md) | **当前唯一执行 Gate** | **是** |
| [V1_PHYSICAL_CAN_EXECUTION_PLAN.md](V1_PHYSICAL_CAN_EXECUTION_PLAN.md) | V1 后若选择物理 CAN 时使用的候选阶段与停止线 | 否；当前冻结 |
| [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) | EtherCAT、Modbus、RT 等长期顺序参考 | 否 |

旧 daemon / ThinkPad / Orange Pi 的 P1–P3 工作包已经完成其计划职责，只作为历史证据地图
保存在 [archive/P1_P3_EXECUTION_PLAN.md](../archive/P1_P3_EXECUTION_PLAN.md)。旧编号不能
覆盖当前 Gate，也不能排下一周任务。

Workbench 有自己的能力合同和局部 Gate，见 [workbench/README.md](../workbench/README.md)，
但在 V1 发布 Gate 关闭前不推进 A2、实物执行器或新页面。

V1 关闭后是先开物理 CAN、EtherCAT 还是另一个独立 Gate，必须重新评审；本目录的文件顺序
不代表已经作出该选择。

全仓文档地图回到 [docs/README.md](../README.md)。职责分区见
[FIVE_LAYERS_ONE_PLANE.md](../FIVE_LAYERS_ONE_PLANE.md)，它不是排期文件。
