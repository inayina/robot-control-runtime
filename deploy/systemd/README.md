# deploy/systemd

本目录将存放 P3-A1 的 systemd unit 与 drop-in 示例。

P3-A0 只冻结路径与所有权合同，**不**提交可运行 unit，避免尚未静态验证的
`ExecStart` 被误当成 Orange Pi 证据。

| 将来文件 | 角色 | 用户 |
|---|---|---|
| `rcr-vcan.service` | oneshot 创建/拉起 `vcan0` | root |
| `rcrd.service` | 前台 Runtime daemon | `rcr` |
| `rcr-node-sim.service` | 验收用节点模拟器（默认 disabled） | 待 A1 定，通常普通用户或 `rcr` |
| `rcrd.service.d/*.conf` | FIFO / affinity 等可选 drop-in | — |

二进制与脚本一律通过 `/opt/robot-control-runtime/current/...` 绝对路径引用，
不依赖 `$PATH`，也不从源码工作区直接 `ExecStart`。

权威路径与回滚合同见 [ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md)。
