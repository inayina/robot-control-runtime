# 计划入口

**Portfolio V1 功能仍冻结**（EtherCAT / ROS 2 / PREEMPT_RT / 新 UI / 新总线：**不做**）。
当前唯一 Active Gate 是本机后续开发 SPEC；Closed-Loop Portfolio Freeze 为
`Deferred / still open`，不得标 CLOSED。

| 文档 | 角色 | 是否决定当前任务 |
|---|---|---|
| [POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md](POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md) | **Current Gate**：本机 Operations/Observability/Diagnostics/Incident；Orange Pi 后置 | **是**；当前内部 milestone 为 **LD4**（本机实现完成，clean acceptance commit 待提交） |
| [CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md](CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md) | Deferred / still open：实物闭环 13 项，第 11 项仍缺 | **否**；不删除已有证据，不因软件测试关闭 |
| [PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md](PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md) | 前置：Physical Modbus RTU backend | 否 |
| [REMOTE_WORKBENCH_BOUNDARY_GATE.md](REMOTE_WORKBENCH_BOUNDARY_GATE.md) | 已关闭：Remote loopback | 否 |
| [MODBUS_IO_MOCK_GATE.md](MODBUS_IO_MOCK_GATE.md) | 已关闭：Modbus I/O Mock | 否 |
| 其余 roadmap / clean-release / EtherCAT 候选 | 历史 / 实验 | 否；冻结后不启动 |

一页对外说明：[PORTFOLIO_SUMMARY.md](../PORTFOLIO_SUMMARY.md)。
