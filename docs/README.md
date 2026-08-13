# 文档怎么读

这里是任务路由，不复制会变化的测试数量、阶段状态或证据结论。仓库区域速查见
[REPOSITORY_MAP.md](REPOSITORY_MAP.md)；系统范围以 [SPEC.md](../SPEC.md) 为准；计划状态从
[plans/README.md](plans/README.md) 读取。最近的 Remote Workbench Boundary 与 Modbus Mock Gate
已关闭，下一 Gate 未选择。

| 如果你想…… | 从这里开始 | 接着读 |
|---|---|---|
| 理解系统架构 | [ARCHITECTURE.md](ARCHITECTURE.md) | [CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md)、[LINUX_RUNTIME.md](LINUX_RUNTIME.md)、[`rcrd` 合同](RCRD_CONTRACT.md) |
| 构建和运行 | [根 README](../README.md#构建与测试) | [最小运行路径](../README.md#最小运行路径) |
| 操作 Orange Pi | [ORANGE_PI_BRINGUP.md](ORANGE_PI_BRINGUP.md) | [部署资产](../deploy/orangepi/README.md)、[硬件拓扑](HARDWARE_TOPOLOGY.md) |
| 查看 physical CAN / STM32 | [硬件拓扑](HARDWARE_TOPOLOGY.md) | [固件 SPEC](../firmware/stm32f103/SPEC.md)、[证据摘要](../evidence/stm32f103_can/README.md) |
| 判断验证强度 | [evidence 入口](../evidence/README.md) | [EVIDENCE_SCHEMA.md](EVIDENCE_SCHEMA.md)、[实时证据 schema](REALTIME_EVIDENCE_SCHEMA.md) |
| 使用 Workbench | [workbench/README.md](workbench/README.md) | [验证条件](workbench/GATES.md)、[Mock 合同](workbench/ACTUATOR.md) |
| 看当前和未来路线 | [plans/README.md](plans/README.md) | Current Gate、physical CAN candidate、长期 roadmap 在此分流 |
| 看系统收敛审计 | [SYSTEM_CONVERGENCE_AUDIT.md](SYSTEM_CONVERGENCE_AUDIT.md) | ownership、能力矩阵、简历边界与下一 Gate 候选 |
| 看 PC→ARM→Device 长期计划 | [PC_ARM_DEVICE_CONVERGENCE_PLAN.md](plans/PC_ARM_DEVICE_CONVERGENCE_PLAN.md) | Reference only；不是实施授权 |
| 学习和准备面试 | [KNOWLEDGE_BASE.md](KNOWLEDGE_BASE.md) | [模块知识卡](MODULE_KNOWLEDGE_CARDS.md)、[Workbench 学习笔记](workbench/NOTES.md) |
| 准备作品集表达 | [portfolio/README.md](portfolio/README.md) | 叙事必须回链工程证据，不能反向定义当前状态 |

总线与 realtime 材料默认是实验、学习或候选 Gate：从
[通信演进边界](COMMUNICATION_EVOLUTION.md)、[EtherCAT NIC Gate](ETHERCAT_NIC_GATE.md)、
[Modbus TCP 笔记](MODBUS_TCP_NOTES.md) 和
[PREEMPT_RT feasibility Gate](PREEMPT_RT_FEASIBILITY_GATE.md) 进入。它们不是 Runtime 已完成
能力。

`archive/` 和 `workbench/archive/` 只保存过期判断与阶段流水，不在主导航承担现状；旧阶段
编号用于对齐历史证据，不是新人理解仓库的前置知识。图片索引见
[images/README.md](images/README.md)，姐妹仓边界见 [SISTER_REPOS.md](SISTER_REPOS.md)。
