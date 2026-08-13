# Repository map

第一次进入仓库先读根 [README](../README.md)；当前执行状态只读
[plans/README](plans/README.md)。最近的
[Remote Workbench Boundary](plans/REMOTE_WORKBENCH_BOUNDARY_GATE.md) 已关闭，下一 Gate 未选择。

| Repository area | Purpose | Start here |
|---|---|---|
| `linux/` | C++20 Runtime、daemon、测试、headless/可选 Qt Workbench | [Architecture](ARCHITECTURE.md) |
| `protocol/` | 冻结的 CAN V1 wire contract 与 golden vectors | [CAN V1 README](../protocol/can_v1/README.md) |
| `deploy/` | systemd、release、Orange Pi bring-up/recovery | [Orange Pi contract](ORANGE_PI_BRINGUP.md) |
| `experiments/` | Modbus、EtherCAT/realtime/multibus 独立实验 | [Development roadmap](plans/DEVELOPMENT_ROADMAP.md) |
| `evidence/` | 可复现结果、环境元数据与受限结论 | [Evidence index](../evidence/README.md) |
| `firmware/` | 可选 MCU 实验边界；不进入 V1 Linux build | [System scope](../SPEC.md) |
| `docs/` | 合同、架构、运维、学习、计划和作品集路由 | [Docs router](README.md) |

代码归属查 [CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md)。`docs/archive/` 与
`docs/workbench/archive/` 只解释历史，不代表当前计划。
