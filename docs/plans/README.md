# 计划入口

**Portfolio V1：FUNCTION FROZEN**（EtherCAT / ROS 2 / PREEMPT_RT / 新 UI / 新总线：**不做**）。

```text
LD0–LD8: CLOSED
Current Active Development Gate: NONE
Closed-Loop Physical Acceptance: OPEN / DEFERRED
Next Development Gate: NOT SELECTED
```

本页是项目当前状态和未来 Gate 选择的唯一 authority。Closed-Loop Portfolio Freeze 不得因软件
测试标为 CLOSED。

| 文档 | 角色 | 是否决定当前任务 |
|---|---|---|
| [POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md](POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md) | **Closed execution record**：LD0–LD8 本机 Operations/Observability/Diagnostics/Incident/Traceability/CI | **否**；LD8 acceptance baseline 为 `b31296f` |
| [CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md](CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md) | OPEN / DEFERRED：实物闭环 13 项，第 11 项仍缺 | **否**；不删除已有证据，不因软件测试关闭 |
| [PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md](PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md) | 前置：Physical Modbus RTU backend | 否 |
| [REMOTE_WORKBENCH_BOUNDARY_GATE.md](REMOTE_WORKBENCH_BOUNDARY_GATE.md) | 已关闭：Remote loopback | 否 |
| [MODBUS_IO_MOCK_GATE.md](MODBUS_IO_MOCK_GATE.md) | 已关闭：Modbus I/O Mock | 否 |
| 其余 roadmap / clean-release / EtherCAT 候选 | 历史 / 实验 | 否；冻结后不启动 |

一页对外说明：[PORTFOLIO_SUMMARY.md](../PORTFOLIO_SUMMARY.md)。
