# deploy/systemd（P3-A1）

本目录存放可静态验证的 systemd unit 与 FIFO/affinity drop-in **示例**。
静态验证通过不等于 Orange Pi 已部署；板上安装与生命周期属 P3-B2。

## 文件

| 文件 | 角色 | 用户 | 默认 |
|---|---|---|---|
| `rcr-vcan.service` | oneshot 创建/拉起 `vcan0` | root | 可 enable |
| `rcrd.service` | 前台 Runtime daemon | `rcr` | 可 enable |
| `rcr-cell-app.service` | RuntimeDaemon + CellReadyMapper + CEL1 主演示宿主 | `rcr` | 后置 physical Gate |
| `rcr-modbus-rtu-agent.service` | 独占 Modbus RTU 串口的低频 agent | `rcr` + `dialout` | 后置 physical Gate |
| `rcr-node-sim.service` | 验收用节点模拟器 | `rcr` | **不**随安装自动 enable |
| `drop-ins/rcrd-fifo-affinity.conf.example` | 可选 FIFO + affinity | — | 默认不安装 |
| `verify_units.sh` | `systemd-analyze verify` + 证据报告 | — | 开发机执行 |

二进制与脚本一律通过 `/opt/robot-control-runtime/current/bin/...` 绝对路径引用。

## 设计要点

- `rcrd` / `rcr-node-sim`：`Type=simple`，stdout/stderr → journal，`TimeoutStopSec=5s`
- `rcr-cell-app` 与 `rcrd` 互相 `Conflicts=`，避免两个 Runtime 宿主同时写同一 CAN interface
- `rcr-modbus-rtu-agent` 不被 cell app `Requires=`；agent 离线时 cell app 仍存活并报告 degraded
- agent 通过 `SupplementaryGroups=dialout` + `DeviceAllow=/dev/ttyS7 rw` 约束串口所有权
- `Restart=on-failure` + `RestartSec=2s`；`StartLimitIntervalSec=30` / `Burst=3`
- 正常 SIGTERM / 退出码 0 不自动重启
- **无** `WatchdogSec=`（尚无 `sd_notify`）
- 基础 unit：`SCHED_OTHER`、不绑核、`RestrictRealtime=yes`
- FIFO/affinity：复制示例 drop-in 到 `/etc/systemd/system/rcrd.service.d/`，改 CPU 号后再启用
- 最小 hardening：`NoNewPrivileges`、`ProtectHome`、`PrivateTmp`；daemon 另加
  `ProtectSystem=strict`。不一次堆叠未验证的地址族/能力限制

## 静态验证（ThinkPad / 开发机）

```bash
chmod +x deploy/systemd/verify_units.sh
./deploy/systemd/verify_units.sh
```

验证时把 ExecStart 临时改写到 stub，避免尚未 `install_release` 时因缺
`/opt/.../current` 二进制而失败。仓库内 unit 原文仍保持部署合同路径。报告写入
`evidence/systemd/analyze_verify_<stamp>.txt`，且 `orange_pi_evidence=false`。

## 板上安装提纲（P3-B2，此处不执行）

```bash
# 假设 release 已安装且 current 已指向
sudo install -m 0644 deploy/systemd/rcr-vcan.service /etc/systemd/system/
sudo install -m 0644 deploy/systemd/rcrd.service /etc/systemd/system/
sudo install -m 0644 deploy/systemd/rcr-cell-app.service /etc/systemd/system/
sudo install -m 0644 deploy/systemd/rcr-modbus-rtu-agent.service /etc/systemd/system/
sudo install -m 0644 deploy/systemd/rcr-node-sim.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now rcr-vcan.service rcrd.service
# 仅验收时：
sudo systemctl enable --now rcr-node-sim.service
```

权威路径与用户合同见 [ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md)。
