# 文档怎么读

第一次读：[根 README](../README.md) → [PORTFOLIO_SUMMARY.md](PORTFOLIO_SUMMARY.md) →
[ARCHITECTURE.md](ARCHITECTURE.md)。**Portfolio V1 功能已冻结；LD0–LD8 已关闭；当前没有 Active
Development Gate。** 项目状态和未来 Gate 选择只读 [plans/README.md](plans/README.md)。
[闭环作品集冻结 Gate](plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md) 为 `OPEN / DEFERRED`。

仓库范围以 [SPEC.md](../SPEC.md) 为准。区域速查：[REPOSITORY_MAP.md](REPOSITORY_MAP.md)。

| 如果你想…… | 从这里开始 | 接着读 |
|---|---|---|
| 理解系统架构 | [ARCHITECTURE.md](ARCHITECTURE.md) | [CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md)、[LINUX_RUNTIME.md](LINUX_RUNTIME.md)、[`rcrd` 合同](RCRD_CONTRACT.md) |
| 核对当前 HEAD 事实 | [HEAD_REALITY_AUDIT.md](HEAD_REALITY_AUDIT.md) | [进程/线程](RUNTIME_PROCESS_THREAD_MODEL.md)、[FD/event](FD_EVENT_MODEL.md) |
| 追溯已关闭 LD0–LD8 | [POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md](plans/POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md) | 当前项目状态见 [计划入口](plans/README.md) |
| 构建和运行 | [根 README](../README.md) | Workbench：[workbench/README.md](workbench/README.md) |
| 操作 Orange Pi | [ORANGE_PI_BRINGUP.md](ORANGE_PI_BRINGUP.md) | [部署资产](../deploy/orangepi/README.md)、[硬件拓扑](HARDWARE_TOPOLOGY.md) |
| STM32 / PA0 | [固件 SPEC](../firmware/stm32f103/SPEC.md) | [独立 CAN 证据](../evidence/stm32f103_can/README.md) |
| 判断验证强度 | [evidence 入口](../evidence/README.md) | [闭环表](../evidence/closed_loop_portfolio/README.md) |
| 准备作品集 / 面试 | [PORTFOLIO_SUMMARY.md](PORTFOLIO_SUMMARY.md) | [口述](portfolio/RESUME_AND_TALK_TRACK.md)、[KNOWLEDGE_BASE.md](KNOWLEDGE_BASE.md) |

LOOPBACK、Actuator MOCK、EtherCAT / PREEMPT_RT 笔记是历史 / 实验材料，不是主演示。
`archive/` 不承担现状。
