# Orange Pi Bring-up 与部署合同（P3-A0）

状态：Frozen（路径 / 用户 / manifest / 回滚）；P3-A2 模板已挂接  
关联：[P1–P3 详细执行记录（归档）](archive/P1_P3_EXECUTION_PLAN.md) §4.2、[deploy/orangepi/PATHS.md](../deploy/orangepi/PATHS.md)、
[BRINGUP_CHECKLIST.md](../deploy/orangepi/BRINGUP_CHECKLIST.md)

本文是 Orange Pi 4 Pro 4GB（及本机自测）部署路径的唯一权威合同。systemd unit 静态资产
属于 **P3-A1（已落地，见 `deploy/systemd/`）**；到货前勾选表与共享矩阵 runner 属于
**P3-A2（已落地）**；到货后的实测属于 **P3-B\***。未执行的板上项不得写成 PASS。

## 0. 目标板与观察边界

已选定目标为 Orange Pi 4 Pro 4GB。采购时的预期规格是 Allwinner A733、4GB LPDDR5、
板载千兆以太网、Wi-Fi 6 和 5V/3A Type-C 供电。以上来自产品资料，不是实测结果。

P3-B0 必须重新记录板卡丝印、`/proc/device-tree/model`、RAM、镜像来源、kernel、DTB、
CPU 大小核拓扑、governor、供电/降频状态、以太网 PHY/驱动和 Wi-Fi 接口。预期与实物
不一致时以观察结果为准并停在当前 Gate，不通过改文档掩盖差异。

P3 只使用 `vcan0`。官方 40-pin 功能列表未声明 CAN，不预设板载 `can0`；板载千兆网口
在 P3 只承担普通网络。后续 EtherCAT 对照若使用该网口，必须独占接口并让管理流量走
Wi-Fi，且不把结果写回 P3 的 `vcan` 证据。

## 1. 解决什么问题

需要在板上用**可回滚、可核对**的方式安装 `rcrd`，而不是把开发树直接当生产路径，
也不是引入 Docker/Ansible 掩盖 systemd 与权限细节。

## 2. 冻结布局

```text
/opt/robot-control-runtime/
  releases/<git-short-sha>/
    bin/rcrd
    bin/rcr_node_sim
    bin/rcr_benchmark
    bin/setup_vcan.sh
    MANIFEST.txt
  current -> releases/<git-short-sha>

/etc/robot-control-runtime/     # 仅 drop-in / 部署元数据；不装 YAML
```

| 路径 | owner:group | mode | 说明 |
|---|---|---|---|
| `/opt/robot-control-runtime/` | `root:root` | `0755` | 安装根 |
| `releases/<id>/` | `root:root` | `0755` | 一份不可变 release |
| `releases/<id>/bin/*` | `root:root` | `0755` | 可执行文件与运维脚本 |
| `releases/<id>/MANIFEST.txt` | `root:root` | `0644` | 版本与校验合同 |
| `current` | `root:root` | symlink | 只指向一份已验证 release |
| `/etc/robot-control-runtime/` | `root:root` | `0755` | A1 起放 drop-in；不放业务 YAML |

**证据目录**：验收脚本写到源码工作区 `evidence/` 或调用者显式目录。周期线程不写文件；
证据默认不进 `/opt`。

## 3. 用户与权限

| 主体 | 职责 | 明确禁止 |
|---|---|---|
| 系统用户 `rcr` | 运行 `rcrd`（A1 unit：`User=rcr`） | 登录 shell、`CAP_NET_ADMIN`、长期 root 等价物 |
| root | `rcr-vcan.service` 调用已安装的 `setup_vcan.sh`；执行安装/回滚 | 用 root 长期跑 `rcrd` |

基础 unit（A1）以 `SCHED_OTHER`、不绑定 CPU 启动。FIFO/affinity 只用显式 drop-in，
并配合 `LimitRTPRIO`；优先不用 `CAP_SYS_NICE`。

不设置 `WatchdogSec=`：当前 `rcrd` 无 `sd_notify` 心跳。

## 4. MANIFEST 字段

每个 release 目录必须有 `MANIFEST.txt`，至少包含：

```text
date_utc=...
hostname=...
machine=...
compiler=...
build_type=...
build_dir=...
git_commit=...
git_short=...
git_dirty=true|false
release_id=...
prefix=...
sha256_rcrd=...
sha256_rcr_node_sim=...
sha256_rcr_benchmark=...
sha256_setup_vcan_sh=...
```

P3 **不**给 `rcrd` 注入构建时 Git 状态，也**不**增加装饰性 `--version`。运行中核对版本
靠 `readlink` + `MANIFEST.txt` + 二进制 SHA-256。

## 5. 构建权威路径

板上（或本机自测）原生 CMake 是首个权威路径：

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

交叉编译只可作为可选加速，不建立统一超级构建，也不改变 release 布局。

## 6. 安装合同

脚本：`deploy/orangepi/install_release.sh`

| 规则 | 行为 |
|---|---|
| 默认模式 | **dry-run**，只打印计划动作 |
| `--apply` | 写入 `releases/<id>/` |
| `--activate` | 额外把 `current` 指到该 release |
| release id | 默认 `git rev-parse --short=12`；须匹配 `[0-9a-f]{7,40}` |
| 目标边界 | `prefix` 必须为绝对路径；拒绝安装进源码树 |
| 覆盖 | 已存在的 `releases/<id>` **拒绝**覆盖 |
| 原子性 | `MANIFEST.txt` 先写临时文件再 `mv` |

本机合同自测可用 `--prefix /tmp/...`，正式板卡使用默认 `/opt/robot-control-runtime`。

```bash
./deploy/orangepi/install_release.sh --build-dir build/linux
sudo ./deploy/orangepi/install_release.sh --apply --activate --build-dir build/linux
```

## 7. 回滚合同

脚本：`deploy/orangepi/rollback_release.sh`

1. 确认目标 `releases/<id>` 存在且含 `MANIFEST.txt` 与 `bin/rcrd`；
2. `ln -sfn releases/<id> current`；
3. 可选 `--restart`：`systemctl try-restart rcrd.service`（A1 落地后）；
4. **不**删除任何 release、源码、证据或未知文件。

```bash
./deploy/orangepi/rollback_release.sh --list --prefix /opt/robot-control-runtime
sudo ./deploy/orangepi/rollback_release.sh --apply --restart <previous-id>
```

## 8. 与后续工作包的边界

| 工作包 | 本文件已覆盖 | 板上状态（2026-08-05） |
|---|---|---|
| P3-A0 | 路径、用户、manifest、安装/回滚脚本 | 板上 `install_release --apply --activate` 已执行 |
| P3-A1 | 引用 `current` 的约定；三个 unit、hardening、verify | unit 已 install/enable；CAN 机制 **unsupported**，`rcrd` inactive |
| P3-A2 | 勾选表、共享 benchmark runner、主机快照 | 勾选表有填本；ARM 矩阵已采 |
| P3-B0–B3 | — | 本地原始证据 + [脱敏入库摘要](../evidence/portfolio/README.md) |
| P3-B4 | — | 冷启动绿灯 **未做** |

## 9. 明确不能声称

- 本文档或 dry-run **不等于** Orange Pi daemon 已常驻；
- ThinkPad 上的 `systemd-analyze verify` / 本机 `enable` **不等于** Orange Pi 冷启动证据；
- 板上 unit **enabled** 但内核 CAN 机制 `unsupported`、`rcrd` inactive，不得写成 Runtime 部署完成；
- `git_dirty=true` 的 release 不能当作干净基线对外叙述；
- 软件联锁 / `SCHED_FIFO` 不是功能安全或硬实时。

## 10. 低风险自测（ThinkPad）

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
PREFIX="$(mktemp -d /tmp/rcr-opt.XXXXXX)"
./deploy/orangepi/install_release.sh --apply --activate \
  --prefix "${PREFIX}" --build-dir build/linux
readlink "${PREFIX}/current"
cat "${PREFIX}/current/MANIFEST.txt"
./deploy/orangepi/install_release.sh --apply --prefix "${PREFIX}" --build-dir build/linux
# 期望：refuse overwrite
./deploy/orangepi/rollback_release.sh --list --prefix "${PREFIX}"
rm -rf "${PREFIX}"
```
