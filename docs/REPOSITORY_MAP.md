# 仓库地图

第一次进入仓库先读根 [README](../README.md) 与 [PORTFOLIO_SUMMARY.md](PORTFOLIO_SUMMARY.md)。
**Portfolio V1 已冻结。** 实物表：[闭环 Gate](plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)。

| 区域 | 用途 | 从这里开始 |
|---|---|---|
| `linux/` | Runtime、`rcrd`、`rcr_cell_app`、可选 Qt | [架构](ARCHITECTURE.md) |
| `protocol/` | CAN V1 线级合同 | [CAN V1 README](../protocol/can_v1/README.md) |
| `firmware/stm32f103/` | CAN 节点：PA8 / PA0 / bxCAN | [固件 SPEC](../firmware/stm32f103/SPEC.md) |
| `deploy/` | Orange Pi 发布 / systemd | [Orange Pi 合同](ORANGE_PI_BRINGUP.md) |
| `evidence/` | 可复现结果；目录 ≠ 通过 | [证据入口](../evidence/README.md) |
| `docs/` | 合同、架构、作品集 | [文档怎么读](README.md) |
| `experiments/` | 历史 / 实验 | 不进入主演示 |

代码归属：[CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md)。
