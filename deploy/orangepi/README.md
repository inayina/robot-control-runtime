# deploy/orangepi

Orange Pi / 本机部署资产：把已构建的 Linux 二进制安装进冻结的 release 布局，
并支持把 `current` 切到上一份明确 release。

| 文件 | 作用 |
|---|---|
| `install_release.sh` | 默认 dry-run；校验后写入 `releases/<id>/` 与 `MANIFEST` |
| `rollback_release.sh` | 默认 dry-run；只切换 `current` 并可选重启服务，不删除任何 release |
| `PATHS.md` | 路径、用户、owner/mode 的短表（与 bring-up 文档一致） |

权威说明：[ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md)。

不引入 Docker、Ansible 或跨架构超级构建。权威构建路径仍是板上原生 CMake。
