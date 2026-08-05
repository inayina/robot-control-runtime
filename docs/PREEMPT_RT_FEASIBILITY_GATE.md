# RT4：PREEMPT_RT 可行性 Gate（Orange Pi 4 Pro）

状态：**Blocked**（板上禁止安装 RT 内核）+ 允许 **Fallback**（ThinkPad 方法对照）  
冻结日期：2026-08-05  
探针：`evidence/realtime_linux/20260805T113914Z_orangepi_rt4_gate/`（只读，未改启动项）

本 Gate **不**安装内核、**不**改 `boot.cmd`/`uImage`。目标只回答：在当前板上是否具备
“可追溯源码 + 并存启动项 + 可验证回退”的条件。

## 1. Gate 判定

| 判定 | 含义 | 本次 |
|---|---|---|
| Pass | 源码/配置可追溯，离线构建成功，启动项并存，回退已静态核对 | **未达到** |
| Blocked | 无可靠源码、补丁不兼容或无可验证恢复/并存路径 | **成立（装核禁止）** |
| Fallback | 先在 ThinkPad 做方法对照；Orange Pi 保留普通内核结论 | **允许** |

**一句话**：当前运行的是厂商普通 `CONFIG_PREEMPT=y` 内核；启动脚本只加载唯一 `uImage`；
`CONFIG_PREEMPT_RT` 未开启；与运行镜像精确对应的源码/补丁/离线 RT 构建均未关闭。  
因此 **不得在 Orange Pi 上安装或覆盖 PREEMPT_RT 内核**。普通内核 RT0–RT3 证据仍然有效。

## 2. 已查清的事实

### 2.1 运行中内核

| 字段 | 值 |
|---|---|
| `uname` | `6.6.98-sun60iw2` `#1.0.8 SMP PREEMPT` |
| 镜像 | Orange Pi 1.0.8 Jammy |
| 包 | `linux-image-current-sun60iw2 1.0.8`（Maintainer: Orange Pi） |
| dpkg `Source` | `linux-6.6.98-sun60iw2` |
| `/sys/kernel/realtime` | **absent** |
| `CONFIG_PREEMPT_RT` | **未设置** |
| 抢占模型 | `CONFIG_PREEMPT=y`（可抢占内核 ≠ PREEMPT_RT） |
| `CONFIG_HZ` | 250 |
| `CONFIG_HIGH_RES_TIMERS` | y |
| config sha256 | `496b843f53923a4d623ced063ac1ec32f925cb9f9a474526094b1496cf6f37e7` |
| uImage sha256 | `75490bcd880ecdd9291a2ee2ffc979305f36d5a5b06bf99a61bb2063bc8082a8` |

uname 里的 `PREEMPT` 只表示标准可抢占；面试时不得说成“已经是 RT 内核”。

### 2.2 源码线索（未钉死到本机二进制）

公开构建树指向：

- 框架：`orangepi-xunlong/orangepi-build`（sun60iw2 / Orange Pi 4 Pro）
- 宣称内核仓：`https://gitee.com/orangepi-xunlong/orange-pi-6.6-sun60iw2.git`
- 分支线索：`orange-pi-6.6-sun60iw2`
- U-Boot 包：`linux-u-boot-orangepi4pro-current 1.0.8`

**缺口**：本 Gate 未 checkout 源码，未证明该仓某 commit 能复现 sha256 与本机
`uImage`/`config` 一致；apt 无匹配的 `linux-source-6.6.98-sun60iw2`。  
因此“源码可追溯”只到**厂商包名 + 公开仓线索**，未到**可复现 hash 闭环**。

### 2.3 PREEMPT_RT 补丁兼容性

- 主线/stable 的 `PREEMPT_RT` 需与具体 6.6.y 版本对齐；
- 厂商 `sun60iw2` 树含板级驱动/补丁，**不能**假定直接打 upstream RT 补丁即可启动 Wi-Fi、
  MMC、显示与电源管理；
- 本次**未**做补丁应用或编译试验。

→ 兼容性：**未知**，按 Gate 记为未关闭风险，足以支撑 Blocked。

### 2.4 启动与文件系统

| 项 | 观察 |
|---|---|
| Boot 脚本 | `/boot/boot.cmd` → `boot.scr`（U-Boot script） |
| Env | `/boot/orangepiEnv.txt`（`fdtfile=allwinner/sun60i-a733-orangepi-4-pro.dtb`） |
| 加载对象 | **固定** `uImage` + `uInitrd` + 单个 DTB |
| 模块 | 仅 `/lib/modules/6.6.98-sun60iw2` |
| 根与 boot | **同一分区** `/dev/mmcblk1p1` |
| 多内核菜单 | **无**（无 extlinux 多条目、无并存第二 `uImage` 选择） |

覆盖 `/boot/uImage` 等于覆盖**唯一**已知可启动内核路径。

### 2.5 恢复链路（物理存在 ≠ Gate Pass）

| 链路 | 状态 |
|---|---|
| UART | cmdline `console=ttyS0,115200`；节点 `/dev/ttyS0` 存在 |
| SSH / Wi-Fi | 管理面可用（本探针即经 SSH） |
| 显示 | 非本 Gate 验证重点 |
| TF 卡重刷 | 理论上可恢复，但是**整卡重装**，不是“第二启动项回退” |

Gate 要求的是**启动项并存 + 失败不覆盖唯一系统**。当前不满足。

## 3. 为何不是 Pass

1. 无与运行 `uImage` 对齐的源码 commit hash / 离线构建产物；  
2. 未验证 sun60iw2 + PREEMPT_RT 补丁可构建、可启动；  
3. `boot.cmd` 无第二内核条目；boot 与 root 同分区，误覆盖风险高；  
4. 未演练“选旧核启动”的静态核对清单（因根本没有旧核条目可演练）。

## 4. Fallback 怎么用（允许）

在 **不改 Orange Pi 内核** 的前提下：

1. 继续把 RT0–RT3 普通内核结果当作板上基线；  
2. 若需学习 PREEMPT_RT **方法**（补丁、`cyclictest`、证据字段），可在 ThinkPad
   用发行版 RT 内核或主线 6.6-rt 做**方法对照**；  
3. ThinkPad 数字**不得**写成 Orange Pi PREEMPT_RT 收益；  
4. 仅当同时满足：源码 commit 钉死、离线构建 hash、双 `uImage`/`boot.cmd` 菜单、
   UART 回退步骤书面核对，才可把本 Gate 从 Blocked 改评审为 Pass，再进入 RT5。

## 5. 明确禁止

- 覆盖唯一 `/boot/uImage` 或唯一 `linux-image-current-sun60iw2`；  
- 刷来源不明的“RT”镜像；  
- 在 Blocked 状态下开始 RT5 同条件对照并宣称板上 RT 收益；  
- 把 `SMP PREEMPT` 说成 PREEMPT_RT。

## 6. 若未来重开 Gate 的最小清单

1. clone 并记录 `orange-pi-6.6-sun60iw2` 的精确 commit；说明与 1.0.8 包的对应关系；  
2. 选定匹配的 `PREEMPT_RT` 补丁版本；记录 reject/冲突；  
3. 离线构建出可安装包或 `uImage`，sha256 入库；  
4. 修改启动为**两套**内核（例如 `uImage` + `uImage-rt`，env 可切换），保留现核；  
5. UART 线可用；书面回退：改回 env / 选旧核 / TF 重刷步骤；  
6. 短 smoke：SSH、存储、Wi-Fi、温度；失败立即回退。

在上述完成前，RT4 保持 **Blocked**。
