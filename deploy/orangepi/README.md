# deploy/orangepi

Orange Pi / 本机部署资产：把已构建的 Linux 二进制安装进冻结的 release 布局，
并支持把 `current` 切到上一份明确 release。

| 文件 | 作用 |
|---|---|
| `install_release.sh` | 默认 dry-run；校验后写入 `releases/<id>/` 与 `MANIFEST` |
| `rollback_release.sh` | 默认 dry-run；只切换 `current` 并可选重启服务，不删除任何 release |
| `rcr_operations.sh` | `status`、只读 `healthcheck`、`collect-logs`、源码侧 `deploy/upgrade`、`rollback` |
| `cel1_status_probe.py` | 默认只读 CEL1 `GetStatus`；`--probe-cell-io` 显式触发边缘 FC02/FC01 恢复探测，不 Activate、不提交输出、不打开 CAN/串口，也不发 FC05 |
| `rcr_observe.py` | `rcr.local_observability.v1` JSON；按 owner/availability/age 输出只读快照 |
| `test_operations.sh` | 临时 prefix + fake systemd/CEL1 的 LD2 合同演练；不改真实 systemd |
| `PATHS.md` | 路径、用户、owner/mode 的短表（与 bring-up 文档一致） |
| `BRINGUP_CHECKLIST.md` | **P3-A2** 到货勾选表（B0–B4）；未实测=`NOT_RUN` |
| `b2_bringup_once.sh` | 板上 release/unit 安装与能力失败归档；无 CAN 时不冒充 active |
| `b3_fifo_matrix_once.sh` | root 权限下采 12 格矩阵；不修改 binary capability |
| `b4_bringup_once.sh` | reboot/回滚辅助；只接受两个真实 release，不制造假版本 |

其他文件按用途分组，文件仍留在这里以保持操作手册和历史命令可复现：

| 组 | 文件 | 边界 |
|---|---|---|
| Realtime 采集 | `rt1_smoke_once.sh`、`rt1_formal_once.sh`、`rt2_cyclictest_once.sh` | 采集普通/实时 Linux 对照；不声明硬实时 |
| Physical CAN 候选 | `PHYSICAL_CAN_BRINGUP_CHECKLIST.md`、`dual_boot_can1.sh`、`debugfs_install_dual.sh` | 受 Gate 约束；stock/can1 与 physical `can0` 分开 |
| U-Boot 启动资产 | `boot-sun60iw2-dual.cmd`、`boot-sun60iw2-dual.scr`、`uboot_install_dual.py` | 双启动/恢复实验，不是默认部署依赖 |
| 串口与恢复工具 | `serial_*.py`、`uboot_recover_*.py`、`uboot_source_stock_scr.py`、`uboot_wait_powercycle_recover.py` | Orange Pi 运维恢复；运行前按主合同确认回滚路径 |

权威说明：[ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md)。
`CONFIG_CAN` / `vcan` 内核启用方案（proposed，不直接改运行内核）：
[ORANGE_PI_CONFIG_CAN_PLAN.md](../../docs/ORANGE_PI_CONFIG_CAN_PLAN.md)。
证据目录约定：[`evidence/orangepi/README.md`](../../evidence/orangepi/README.md)。
板上填表示例：`evidence/orangepi/20260805T084028Z/`（B0–B3；**stock** 无 CAN → `rcrd` 未 active）。can1 软件链见 `docs/ORANGE_PI_CONFIG_CAN_PLAN.md`，不能回溯改写这份表。

共享矩阵 runner：`linux/scripts/run_benchmark_matrix.sh`；
板上入口：`./linux/scripts/run_orangepi_benchmark_matrix.sh`。

不引入 Docker、Ansible 或跨架构超级构建。权威构建路径仍是板上原生 CMake。
