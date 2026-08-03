# 部署路径短表（P3-A0 冻结）

与 [ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md) 必须保持一致；冲突时以 bring-up
文档为准并同步修正本表。

## 目录

| 路径 | 用途 | owner:group | mode |
|---|---|---|---|
| `/opt/robot-control-runtime/` | 安装根 | `root:root` | `0755` |
| `/opt/robot-control-runtime/releases/<git-short-sha>/` | 不可变 release | `root:root` | `0755` |
| `.../bin/rcrd` 等可执行文件 | 运行入口 | `root:root` | `0755` |
| `.../MANIFEST.txt` | commit/dirty/compiler/SHA-256 | `root:root` | `0644` |
| `/opt/robot-control-runtime/current` | 指向一份已验证 release 的符号链接 | `root:root` | `0777`（symlink） |
| `/etc/robot-control-runtime/` | drop-in / 部署元数据，**不**装 YAML | `root:root` | `0755` |

## 用户与权限

| 主体 | 约束 |
|---|---|
| 系统用户 `rcr` | 无登录 shell；运行 `rcrd`；**无** `CAP_NET_ADMIN` |
| root | 仅 `rcr-vcan.service` / 安装脚本需要 |

## 证据

验收与 benchmark 证据写在**源码工作区** `evidence/` 或调用者显式给出的目录。
不把证据默认写进 `/opt`，也不由周期线程写文件。

## 回滚

只把 `current` 指到上一份已存在的 `releases/<id>`，然后重启相关 systemd 单元。
不删除源码、证据、未知文件或旧 release。
