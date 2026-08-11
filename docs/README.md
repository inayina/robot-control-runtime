# 文档怎么读

`docs/` 只做知识和证据的路由，不建立第三套架构，也不在入口复制易变化的测试数量。
当前状态、证据边界和退出条件只在
[V1 发布 Gate](plans/PORTFOLIO_V1_RELEASE_PLAN.md) 更新。

系统边界：[SPEC.md](../SPEC.md)、[AGENTS.md](../AGENTS.md)。  
学习/面试：[KNOWLEDGE_BASE.md](KNOWLEDGE_BASE.md)，模块卡
[MODULE_KNOWLEDGE_CARDS.md](MODULE_KNOWLEDGE_CARDS.md)。

## 现在听谁的

| 问题 | 听这篇 | 边界 |
|---|---|---|
| 当前任务与对外边界 | [plans/PORTFOLIO_V1_RELEASE_PLAN.md](plans/PORTFOLIO_V1_RELEASE_PLAN.md) | 唯一执行 Gate |
| Runtime 组件怎么配合 | [ARCHITECTURE.md](ARCHITECTURE.md) + [LINUX_RUNTIME.md](LINUX_RUNTIME.md) | 原理，不负责排期 |
| Runtime 职责区 / A–G | [FIVE_LAYERS_ONE_PLANE.md](FIVE_LAYERS_ONE_PLANE.md) | 不用于拆 Workbench |
| V1 后若选择物理 CAN | [plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md](plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md) | 候选方案；当前冻结 |
| 总线 / RT 长期顺序 | [plans/DEVELOPMENT_ROADMAP.md](plans/DEVELOPMENT_ROADMAP.md) | 参考，不报告当前进度 |
| Workbench 怎么跑 / 分层 | [workbench/README.md](workbench/README.md) | 可选消费者，不覆盖主线 |

计划角色总表见 [plans/README.md](plans/README.md)。

## 按你想做的事

**把 Runtime 讲清楚**

- [ARCHITECTURE.md](ARCHITECTURE.md) — ThinkPad / Orange Pi、线程、命令准入
- [LINUX_RUNTIME.md](LINUX_RUNTIME.md) — 已实现模块怎么串
- [RCRD_CONTRACT.md](RCRD_CONTRACT.md) — `rcrd` 进程合同
- [EVIDENCE_SCHEMA.md](EVIDENCE_SCHEMA.md) — 证据字段
- [ADR-002](ADR-002-minimal-linux-runtime.md) — 为什么收成 Linux Runtime

**部署 / 板子**

- [ORANGE_PI_BRINGUP.md](ORANGE_PI_BRINGUP.md) — 路径、用户、安装、回滚
- [ORANGE_PI_CONFIG_CAN_PLAN.md](ORANGE_PI_CONFIG_CAN_PLAN.md) — 内核档 1 / 还缺档 2
- [HARDWARE_TOPOLOGY.md](HARDWARE_TOPOLOGY.md) — 现有硬件，不是购物清单
- [SISTER_REPOS.md](SISTER_REPOS.md) — 和其他仓的边界

**总线实验（多数还不是 Runtime 功能）**

- [COMMUNICATION_EVOLUTION.md](COMMUNICATION_EVOLUTION.md) — CAN / Modbus / EtherCAT 为什么不能合成 ITransport
- [ETHERCAT_NIC_GATE.md](ETHERCAT_NIC_GATE.md) + [ETHERCAT_PROTOCOL_NOTES.md](ETHERCAT_PROTOCOL_NOTES.md)
- [MODBUS_TCP_NOTES.md](MODBUS_TCP_NOTES.md)
- [OBSERVATION_TO_EXECUTION_CONTRACT.md](OBSERVATION_TO_EXECUTION_CONTRACT.md) — 观测→执行，**未实现**

**实时对照**

- [REALTIME_LINUX_LEARNING_PLAN.md](REALTIME_LINUX_LEARNING_PLAN.md)
- [REALTIME_EVIDENCE_SCHEMA.md](REALTIME_EVIDENCE_SCHEMA.md)
- [PREEMPT_RT_FEASIBILITY_GATE.md](PREEMPT_RT_FEASIBILITY_GATE.md) — Orange Pi 上 **Blocked**

**Workbench（可选 Qt，另一套分层）**

- [workbench/README.md](workbench/README.md) — 入口、目录、怎么跑
- [workbench/GATES.md](workbench/GATES.md) — 还开着的门
- [workbench/NOTES.md](workbench/NOTES.md) — 没学过 Qt
- [workbench/ACTUATOR.md](workbench/ACTUATOR.md) — Actuator 01 Mock
- 流水账：[workbench/archive/PHASE_HISTORY.md](workbench/archive/PHASE_HISTORY.md)

**作品集对外讲**

- [portfolio/README.md](portfolio/README.md)

**图**

- [images/README.md](images/README.md)

## 归档（默认别当现状）

| 文档 | 为什么在 archive |
|---|---|
| [CURRENT_PHASE_PLAN.md](archive/CURRENT_PHASE_PLAN.md) | 2026-08-01 审计，当时下一目标还是 `rcrd` |
| [P1_P3_EXECUTION_PLAN.md](archive/P1_P3_EXECUTION_PLAN.md) | 旧 daemon / ThinkPad / Orange Pi 工作包与编号 |
| [STM32_CAN_SG90_EXPERIMENT.md](archive/STM32_CAN_SG90_EXPERIMENT.md) | 只设计，未采购未做 |
| [ADR-001](archive/ADR-001-f411-no-native-can.md) | 已被 ADR-002 取代 |

## 目录约定

- 顶层：还在用的合同、原理、知识库
- `plans/`：唯一当前 Gate、后续阶段编号和长期顺序；先读那里的 README
- `workbench/`：应用/展示平面，**不要**用五层一横去拆
- `archive/`：过期判断，保留是为了对证据日期
- `portfolio/`、`images/`：对外叙事和图
