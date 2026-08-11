# EtherCAT 主站网卡 Gate（ThinkPad · 零采购）

状态：Living  
对应路线图：`docs/plans/DEVELOPMENT_ROADMAP.md` §8.6
协议预习：`docs/ETHERCAT_PROTOCOL_NOTES.md`（建议先读「怎样读 EtherCAT 预习材料」）  
采集脚本：`linux/scripts/collect_ethercat_nic_gate.sh`  
证据目录：`evidence/ethercat_nic_gate/`（每次探测一个带 UTC 时间戳的子目录；**不**把单次结果当永久配置）

## 1. Gate 要回答什么

在购买 EtherCAT I/O SubDevice 之前，确认 ThinkPad 板载有线网口能否作为 **SOEM 用户态主站** 的候选口：

1. 网卡身份可复核（PCI ID、驱动、内核）；
2. `AF_PACKET` / EtherType `0x88A4` raw bind 可用；
3. 默认管理流量走 Wi-Fi，有线口可留给 EtherCAT；
4. NetworkManager/DHCP **不会**在插上从站网线后抢走该口。

本 Gate **不**证明：进 OP、PDO、WKC、周期确定性、PREEMPT_RT、功能安全。

### 1.1 为什么要先测这些（而不是先买从站）

EtherCAT 主站要在**专用网卡**上发二层 `0x88A4` 帧。常见失败不是“协议看不懂”，而是：

| 现场现象 | 常见根因 | 本 Gate 对应 |
|---|---|---|
| SOEM 打不开适配器 / 权限错 | 无 root/`CAP_NET_RAW`；口名错 | G2、G5 |
| 插上从站后口突然有了 IP、默认路由飘了 | NM/DHCP 抢管有线口 | G3、G4 |
| 换内核/笔记本后“以前能跑” | 驱动/PCI/接口名变了 | G1、复跑 |
| 以为主站验完了 | 把空扫成功当成 PDO PASS | 见 §1.2 / §8 |

零采购先把 host 前置条件钉住，再花钱买从站，避免把“系统管理问题”和“总线协议问题”绞在一起。

### 1.2 G1–G6 各是什么意思（口述）

| ID | 一句话 | 通过时你只知道… | 通过时仍不知道… |
|---|---|---|---|
| **G1** | 这张口是谁（PCI/驱动/接口名） | 证据可复核、换机能对照 | 周期是否稳 |
| **G2** | root 下能否 raw bind `0x88A4` | 权限与 `CONFIG_PACKET` 够用 | 帧内容、WKC、实时性 |
| **G3** | 默认路由是否在 Wi-Fi | 日常上网不必靠实验有线口 | 有线口上是否曾有过 DHCP |
| **G4** | NM 是否不再托管实验口 | carrier 上来时不太会抢 DHCP | 从站通信是否成功 |
| **G5** | SOEM 能否 `ecx_init` 打开该口 | 工具链与适配器打开路径 | 链上有无从站、能否进 OP |
| **G6** | 干净 commit 上复跑过 | 叙述可引用冻结快照 | 业务功能；换硬件仍要复跑 |

**总判读习惯**：G1–G5 齐 = “可以采购简单 I/O 并从联调准备往下走”；**不等于** EtherCAT 主站功能验收。G6 未关时，对外叙述应保留 `git_dirty` / 时间戳边界。

## 2. 检查表

| ID | 检查项 | 最新快照结论（见 §3） | Gate |
|---|---|---|---|
| G1 | 接口名仍为实验口，PCI/驱动可记录 | `enp0s31f6` · `8086:57A1` · `e1000e` | **通过（快照）** |
| G2 | 内核 `CONFIG_PACKET` 可用；root 下 raw bind `0x88A4` | `raw_frame_bind=pass` | **通过（快照）** |
| G3 | 默认路由在 Wi-Fi，不在实验有线口 | `default via … dev wlp0s20f3` | **通过（快照）** |
| G4 | NM 不管理实验口 / 无有线 DHCP autoconnect | `nm_managed=no` · state unmanaged · Wired autoconnect=no | **通过（快照）** |
| G5 | SOEM 能 `ecx_init` 打开该口（无从站可空扫） | `ecx_init … succeeded`；`No slaves found!` | **通过（工具链）** |
| G6 | 正式证据在干净 commit 上复跑 | 本次仍 `git_dirty=true` | **未关闭** |

**总判定（2026-08-04T06:32:53Z 快照）**：零采购 NIC 前置条件 **G1–G5 已满足**（含 NM 独占）。**G6** 仍待干净 commit 复跑后，才宜把本 Gate 写成“已冻结基线”。有从站前仍不得声称 PDO/OP/周期合格。

## 3. 最新快照（一次探测 ≠ 永久）

| 字段 | 值 |
|---|---|
| 快照目录 | `evidence/ethercat_nic_gate/probe_20260804T063253Z/` |
| 先前对照 | `probe_20260804T062941Z/`（NM 托管时）；`probe_20260804T062855Z/`（非完整权限试跑） |
| `date_utc` | `2026-08-04T06:32:53Z` |
| `hostname` | `ina-Gen6` |
| `os_kernel` | `Linux 7.0.0-28-generic x86_64` |
| `git_commit` | `8a2fb120da59f7b5ca204f9cc49b5b93f37ce2b4` |
| `git_dirty` | `true`（临时对照；正式叙述需干净 commit 复跑） |
| 接口 | `enp0s31f6` |
| MAC | `a8:2b:dd:18:fe:88` |
| PCI | `0000:00:1f.6` · `PCI_ID=8086:57A1` · subsystem `17AA:512B`（Lenovo） |
| 驱动 | `e1000e` · `ethtool -i` version `7.0.0-28-generic` · firmware `0.1-4` |
| 链路 | carrier off / 未接从站（预期） |
| raw bind | **pass**（`AF_PACKET` + EtherType `0x88A4`，uid 0） |
| SOEM | `ecx_init` 成功；`No slaves found!` |
| 管理面 | 默认路由 `wlp0s20f3` · `192.168.1.8/24`（Wi-Fi） |
| NM | **unmanaged**（`NM-MANAGED=no`，state `10`）；`/etc/NetworkManager/conf.d/99-rcr-ethercat-unmanaged.conf` 已安装；`Wired connection 1` **AUTOCONNECT=no** |

原始文件：同目录下 `SUMMARY.txt`、`nic_identity.txt`、`raw_frame.txt`、`routing.txt`、`nm_and_packet.txt`、`soem_slaveinfo.txt`。

读快照时建议顺序：`SUMMARY.txt` → `raw_frame.txt` / `routing.txt` → 需要时再翻 identity / SOEM 全文。

## 4. 复跑命令

```bash
cd /home/ina/dev/robot-control-runtime
sudo ./linux/scripts/collect_ethercat_nic_gate.sh
# 可选：sudo ./linux/scripts/collect_ethercat_nic_gate.sh --iface enp0s31f6
```

内核升级、更换网卡、改 NM、或更换默认路由后必须复跑，并更新本文件 §2/§3 指向**新**时间戳目录。

无 root 也能跑：会留下 identity/路由/NM 只读摘要，但 `raw_frame_bind=permission_denied`，
**不能**据此把 G2 写成通过。

## 5. G4：消除 NM/DHCP 干扰（插从站前必做）

### 为什么必须做

当前风险：carrier 一旦出现（插上从站或交换机），NetworkManager 可能按
`Wired connection 1` **自动 DHCP**。结果是：

- 实验口突然有了 IPv4，甚至默认路由从 Wi-Fi 漂走；
- SOEM / raw 主站与“另一套想管网卡的软件”争用同一物理口；
- 故障表现像“EtherCAT 不稳”，实际是 **Linux 网络管理干扰**。

因此 G4 测的是**管理面隔离**，不是总线协议。

### 怎么做

仓库资产（可审查，不直接改 `/etc` 直到你加 `--apply`）：

- 配置：`deploy/ethercat/nm/99-rcr-ethercat-unmanaged.conf`
- 应用：`deploy/ethercat/apply_nm_unmanaged.sh`（默认 dry-run）

```bash
# 预览
sudo ./deploy/ethercat/apply_nm_unmanaged.sh

# 写入 conf.d、关闭 Wired connection 1 的 autoconnect、reload NM
sudo ./deploy/ethercat/apply_nm_unmanaged.sh --apply

# 复跑 Gate 探测并更新本文件 §2/§3 时间戳
sudo ./linux/scripts/collect_ethercat_nic_gate.sh

# 回退
sudo ./deploy/ethercat/apply_nm_unmanaged.sh --apply --revert
```

验收：`nmcli` 显示该口 unmanaged / NM-MANAGED=no；`enp0s31f6` 上无 IPv4；默认路由仍在 Wi-Fi。
连接显示名若不是 `Wired connection 1`，用
`RCR_ECAT_WIRED_CONN='...'` 或 `--wired-conn` 覆盖。

回退：`--apply --revert` 删除 conf.d 条目并尝试恢复 autoconnect。

## 6. SOEM 工具链（本机已具备）

树内第三方克隆（应 gitignore，不进主仓产品代码；**不要修改 SOEM 源码**）：

```text
SOEM/                          # origin OpenEtherCATsociety/SOEM
SOEM/build/libsoem.a
SOEM/build/samples/slaveinfo/slaveinfo
```

重建：

```bash
cd SOEM && cmake -S . -B build && cmake --build build -j
sudo ./build/samples/slaveinfo/slaveinfo enp0s31f6
```

无从站时 `No slaves found!` + `ecx_init … succeeded` 只证明**适配器打开路径**，不是 SubDevice Gate，
更不是 PDO/OP/WKC。接到从站后，同一命令才开始回答“链上有谁”。

## 7. 关闭条件（何时算 Gate 绿灯）

- [x] G1 身份记录与复跑脚本存在  
- [x] G2 raw bind 在某次 root 快照为 pass  
- [x] G3 管理面默认走 Wi-Fi  
- [x] G4 NM unmanaged + 有线 autoconnect 关闭（`probe_20260804T063253Z`）  
- [x] G5 SOEM `ecx_init` 成功  
- [ ] G6 干净 commit 上复跑，摘要写入本文件并更新时间戳  

G1–G5 已够支撑“可采购简单 I/O SubDevice 并做联调准备”。**G6** 关闭前，对外叙述仍应带 `git_dirty=true` 快照边界。有从站前不把 ThinkPad 写成“EtherCAT 主站功能已验收”。

## 8. 不能声称

- 一次 `probe_*` = 换内核/BIOS/网卡后仍成立（须复跑）；  
- raw bind pass = 实时周期合格，或已发过合法业务帧；  
- `No slaves found` = 总线故障（当前无载波时预期）或 = 已验证从站；  
- G1–G5 通过 = INIT→OP / PDO / WKC / 掉线恢复已测；  
- e1000e + SOEM = 已通过工业主站认证；  
- ThinkPad 本 Gate = Orange Pi 板载口同结论（ARM 对照是另一次证据）。

证据等级提醒：本文件是 **host 前置条件快照**；协议理解见笔记；SubDevice 联调仍是后续阶段。
