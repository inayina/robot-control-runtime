# deploy/orangepi

Orange Pi / 本机部署资产：把已构建的 Linux 二进制安装进冻结的 release 布局，
并支持把 `current` 切到上一份明确 release。

| 文件 | 作用 |
|---|---|
| `install_release.sh` | 默认 dry-run；校验后写入 `releases/<id>/` 与 `MANIFEST` |
| `rollback_release.sh` | 默认 dry-run；只切换 `current` 并可选重启服务，不删除任何 release |
| `PATHS.md` | 路径、用户、owner/mode 的短表（与 bring-up 文档一致） |
| `BRINGUP_CHECKLIST.md` | **P3-A2** 到货勾选表（B0–B4）；未实测=`NOT_RUN` |
| `b2_bringup_once.sh` | 板上 release/unit 安装与能力失败归档；无 CAN 时不冒充 active |
| `b3_fifo_matrix_once.sh` | root 权限下采 12 格矩阵；不修改 binary capability |
| `b4_bringup_once.sh` | reboot/回滚辅助；只接受两个真实 release，不制造假版本 |

权威说明：[ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md)。
证据目录约定：[`evidence/orangepi/README.md`](../../evidence/orangepi/README.md)。
板上填表示例：`evidence/orangepi/20260805T084028Z/`（B0–B3；无 CAN → `rcrd` 未 active）。

共享矩阵 runner：`linux/scripts/run_benchmark_matrix.sh`；
板上入口：`./linux/scripts/run_orangepi_benchmark_matrix.sh`。

不引入 Docker、Ansible 或跨架构超级构建。权威构建路径仍是板上原生 CMake。
