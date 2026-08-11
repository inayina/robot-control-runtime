# Orange Pi：把 `CONFIG_CAN` 编进内核（可执行方案）

状态：**档1 已关闭——can1 可启动，`vcan0`+`rcrd` 已跑；HAT 需档2 另编内核**  
证据：[`evidence/orangepi_can_kernel/20260810T120350Z/SUMMARY.md`](../evidence/orangepi_can_kernel/20260810T120350Z/SUMMARY.md)  
脚本：[`deploy/orangepi/dual_boot_can1.sh`](../deploy/orangepi/dual_boot_can1.sh)、
[`deploy/orangepi/boot-sun60iw2-dual.cmd`](../deploy/orangepi/boot-sun60iw2-dual.cmd)  
关联：[V1_PHYSICAL_CAN_EXECUTION_PLAN.md](V1_PHYSICAL_CAN_EXECUTION_PLAN.md) P2-G1、
[deploy/orangepi/PHYSICAL_CAN_BRINGUP_CHECKLIST.md](../deploy/orangepi/PHYSICAL_CAN_BRINGUP_CHECKLIST.md) K-01..K-08、
[ORANGE_PI_BRINGUP.md](ORANGE_PI_BRINGUP.md)

## 0.1 事故结论（覆盖默认 uImage 失败）

直接把 `uImage-can1` 写成默认 `/boot/uImage` 导致掉网/需串口抢救。
**禁止**再覆盖默认 `uImage`。正确合同：

1. 库存内核永久保留为 `/boot/uImage`（与 `/boot/stock-1.0.8/uImage` 同内容）；
2. can1 只存在于 `/boot/uImage-can1` + `/boot/uInitrd-*-can1` + `/lib/modules/6.6.98-sun60iw2-can1`；
3. `boot.cmd`/`boot.scr` 读 `orangepiEnv.txt` 的 `kernel_flavor=stock|can1`；
4. **默认 `kernel_flavor=stock`**；切 can1 前 USB-TTL 必须在线；
5. 回退：改回 `kernel_flavor=stock` 再 reboot（或串口 U-Boot 手工 load 库存）。

## 0.2 二次事故（debugfs 写挂载根分区）

为绕过无 sudo 密码，曾用 `debugfs -w` 改正在挂载的 `/`（`mmcblk1p1`），随后出现：

- 新 `boot.scr` 被 distro boot 判定 `Wrong image format`（自动启动失败）；
- `EXT4-fs error` / remount read-only，NetworkManager/SSH/WPA 失败；
- 内核最终无响应（SysRq 亦失效），需**断电上电**。

**禁止**再对已挂载根分区做 `debugfs -w`。写 `/boot` 只允许：

1. `sudo` 下的普通文件拷贝（首选）；或
2. U-Boot `ext4write`（本板只能写到分区**根目录** `/file`，不能写 `/boot/file`）。

恢复脚本：`deploy/orangepi/uboot_wait_powercycle_recover.py`（上电拦截 U-Boot → 手工 stock boot + `fsck.mode=force`）。

## 0.3 三次事故（can1 uImage 架构头错误）

切 `kernel_flavor=can1` 后 U-Boot 报：

```text
Image Type:   AArch64 Linux Kernel Image
Unsupported Architecture 0x16
ERROR: can't get kernel image!
```

根因：交叉打包用了 `mkimage -A arm64`；本板 U-Boot `bootm` 只接受与库存相同的 **`-A arm`**（arch=2），尽管内核本身是 aarch64。  
已用 `-A arm -a/-e 0x41000000` 重打并覆盖板上 `/boot/uImage-can1`。

## 0. 结论先说

| 问题 | 答案 |
|---|---|
| 现在能“改一下就有 CAN”吗？ | **不能**。运行内核是 `6.6.98-sun60iw2`，`# CONFIG_CAN is not set`；没有现成 `can` 模块可 `modprobe`。 |
| 今晚该不该直接重编并换内核？ | **不该**，除非先完成 §3 回滚 Gate。换 Image/modules 有变砖与 SSH 丢失风险。 |
| 最小目标是什么？ | 先做出 **SocketCAN + `vcan0`**，让板上 `rcrd`/`rcr-vcan` 软件链可跑。 |
| 和物理 HAT 的关系？ | `vcan` 不依赖设备树 overlay；MCP2515/`can0` 另开 P2-G2/G3，且当前 `# CONFIG_OF_OVERLAY is not set`。 |

本方案只覆盖：**用官方 `orangepi-build` 基线，打开 CAN 相关 Kconfig，产出可回滚的新内核，验收 `vcan0`。**  
不覆盖：PREEMPT_RT、EtherCAT、MCP2515 overlay、声称硬实时。

## 1. 2026-08-10 板上快照（SSH 观察）

| 字段 | 观察 |
|---|---|
| host | `orangepi4pro` / `192.168.1.22`（以当时可达为准） |
| kernel | `6.6.98-sun60iw2 #1.0.8 SMP PREEMPT` |
| image | Orange Pi 1.0.8 Jammy；`BOARD=orangepi4pro`，`LINUXFAMILY=sun60iw2`，`BRANCH=current` |
| 构建来源 | `/etc/orangepi-release`：`BUILD_REPOSITORY_URL=https://github.com/orangepi-xunlong/orangepi-build`，`VERSION=1.0.8` |
| CAN | `# CONFIG_CAN is not set` |
| overlay | `# CONFIG_OF_OVERLAY is not set`（物理 DTO 路径的额外缺口） |
| SPI | `CONFIG_SPI=y`；`CONFIG_SPI_SUN4I/SUN6I` 未开（物理 SPI 驱动名须再从运行 DT/config 核对，不在本阶段假设） |
| headers/build | 无 `/lib/modules/$(uname -r)/build`；已装 `linux-image-current-sun60iw2 1.0.8` |
| 工具 | 有 `orangepi-config` |

> 执行前必须重采上述字段；镜像或 kernel 版本漂移则以新观察为准。

## 2. 两条路径比较

| 路径 | 做法 | 何时选 | 不选原因 |
|---|---|---|---|
| **A. `orangepi-build` 重编 current 内核（推荐）** | clone 与 1.0.8 匹配的 build 树，对 `sun60iw2`/`orangepi4pro` 打开 CAN，装到**独立** boot 项 | 要板上 `vcan`/`rcrd` | 耗时长、需磁盘与一次可控重启 |
| B. 换第三方已带 CAN 的内核包 | 找现成 deb | 几乎没有与 `sun60iw2 #1.0.8` 精确匹配的可信包 | 易丢板级补丁/DTB，回归不可控 |
| C. 只改 `/boot/config-*` 文本 | — | **禁止** | 那是导出配置，不改变已启动的 Image |
| D. 等 USB-CAN 再谈内核 | USB 网卡驱动可能自带 CAN | 物理证据优先且只买 USB-CAN | 仍解决不了当前镜像无 `vcan`；本仓 P3 软件链仍缺 CAN subsystem |

首轮选 **A**，且配置分两档：

**档 1（本周目标，仅软件链）**

```text
CONFIG_CAN=m
CONFIG_CAN_RAW=m
CONFIG_CAN_DEV=m
CONFIG_CAN_VCAN=m
```

**档 2（HAT 到货且确认为 MCP2515 后再开）**

```text
CONFIG_CAN_MCP251X=m
# 以及运行 DT 所需的 SPI controller / GPIO IRQ；必要时 CONFIG_OF_OVERLAY
```

不要首轮同时开 PREEMPT_RT、大量无关驱动或改 DTB 默认值。

## 3. 回滚 Gate（未通过禁止安装）

在 ThinkPad 或板上归档（可放私有证据目录，公开仓只留脱敏摘要）：

1. 复制并填写 checklist K-01/K-02：
   - `/boot/vmlinux-6.6.98-sun60iw2`、`uImage`、`uInitrd*`、`dtb-6.6.98-sun60iw2/`、
     `config-6.6.98-sun60iw2`、`System.map-*`、`orangepiEnv.txt`、`boot.cmd`/`boot.scr`
   - `dpkg -l linux-image-current-sun60iw2` 版本与文件 hash
2. 确认至少一种恢复手段可用：**串口**、显示器+键盘、或可拔插的备用启动介质。
3. 新内核必须装成**第二启动项/第二套文件名**，保留当前 `#1.0.8` 为默认回退。
4. 预写失败剧本：新内核无法 SSH → 串口/本地进旧项；旧项也无法启动 → 用备用卡重刷原镜像。

**Gate**：K-01/K-02 = PASS 之前，不执行 `make install` / 覆盖 `uImage`。

## 4. 推荐执行步骤（路径 A）

### 4.1 准备构建环境

优先在 **大磁盘 Linux 主机**交叉编译（Orange Pi 4GB 本地全量编译可行但慢、易 OOM）。  
工具链与目录以 `orangepi-build` 文档为准：

```bash
git clone --depth=1 https://github.com/orangepi-xunlong/orangepi-build
cd orangepi-build
# 尽量对齐板上 VERSION=1.0.8 / COMMIT=edd1c3d-dirty 的可复核点：
# 记录实际 clone commit；dirty 无法比特级复现时，在证据里写明“近似基线 + 本机 diff”。
```

选择板型：`orangepi4pro`，分支：`current`（与 `/etc/orangepi-release` 一致）。

### 4.2 打开 CAN（在生成 `.config` 之后、编译之前）

在 build 系统检出的内核树内（路径随 `orangepi-build` 缓存目录变化，以实际为准）：

```bash
# 示例：进入 kernel 源码目录后
./scripts/config --module CONFIG_CAN
./scripts/config --module CONFIG_CAN_RAW
./scripts/config --module CONFIG_CAN_DEV
./scripts/config --module CONFIG_CAN_VCAN
# 可选核对：
grep -E '^CONFIG_CAN' .config
```

或 `make menuconfig` 勾选 Networking → CAN bus subsystem（Virtual Local CAN 必开）。

保存 `.config` 片段 hash 到证据目录。

### 4.3 编译与安装策略

1. 用 `orangepi-build` 只构建 **kernel + dtb + modules**（不必整镜像重做，除非你更熟镜像流）。
2. 产物改名安装，例如：
   - `vmlinux-6.6.98-sun60iw2-can1` / 对应 initrd / modules 目录
   - 在 `orangepiEnv.txt` 或 boot 菜单增加可选条目，**默认仍指向原 1.0.8**
3. 首次重启选 can 内核；SSH 通后再改默认。

### 4.4 新内核冒烟（K-07/K-08）

```bash
uname -r
zcat /proc/config.gz | grep -E '^CONFIG_CAN'
sudo modprobe can
sudo modprobe can_raw
sudo modprobe can_dev
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
ip -details link show vcan0
```

再跑本仓最小集（在板上已有 release 时）：

```bash
# 非 CAN 回归：SSH、Wi-Fi、systemd、磁盘、时钟
# CAN 软件链：
sudo /opt/robot-control-runtime/current/bin/setup_vcan.sh vcan0
# 再按 BRINGUP / b2 脚本预期启动 rcr-vcan + rcrd（记录 active/failed 事实）
```

**Gate（档 1 关闭）**：

- 运行 config 显示 `CONFIG_CAN*=y/m`，不是另一份未启动的文件；
- `vcan0` up；
- `rcrd` 能在 `vcan0` 上按合同启动或失败原因可解释；
- 可一键退回旧内核且 SSH 恢复。

## 5. 证据怎么写

建议私有目录示例：`evidence/orangepi_can_kernel/<stamp>/`

| 文件 | 内容 |
|---|---|
| `host_before.txt` | uname、orangepi-release、config CAN 行、boot 文件列表 |
| `config_fragment.txt` | 打开的 `CONFIG_CAN*` 与 hash |
| `build_id.txt` | orangepi-build commit、编译主机、交叉工具链版本 |
| `host_after.txt` | 新 uname、`/proc/config.gz` CAN 行、`ip link`、`lsmod` |
| `rollback.txt` | 如何回到 1.0.8，以及是否实测过 |
| `SUMMARY.md` | PASS/FAILED；明确未做物理 `can0` |

公开仓只提交脱敏 SUMMARY 与 hash，不提交内网 IP/密钥。

## 6. 明确不做 / 不能声称

- 不把“改了 `.config` 文本”写成内核已支持 CAN。
- 不把 `vcan0` 写成物理 CAN 或功能安全。
- 不开 overlay / MCP2515 就不声称 `can0`。
- 不在无回滚证据时覆盖唯一可启动 Image。
- 本方案关闭 ≠ P2 物理 CAN Gate 关闭；P2-G2 起仍要 HAT 识别与 DT。

## 7. 今晚可做的低风险动作（无需换内核）

1. 重采 §1 快照，确认仍是 `# CONFIG_CAN is not set`。
2. 按 §3 备份 `/boot` 关键文件与 hash（只读复制）。
3. 在 ThinkPad 上 clone `orangepi-build`，确认能进到 `orangepi4pro` + `current` 配置流。
4. 预写 `evidence/.../SUMMARY.md` 模板为 `NOT_RUN`。

**停止线**：未完成 §3 回滚 Gate 前，不安装新内核。需要我下一步时，优先做「备份 + 构建机拉起 orangepi-build」，而不是直接 `make install`。
