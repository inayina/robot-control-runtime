# 零采购作品集 V1 发布计划

状态：Active（当前唯一执行 Gate）
冻结日期：2026-08-05；入口收敛：2026-08-11
目标：不依赖新增通信实验硬件，形成一版可公开、可复现、证据边界准确的作品集。已在途
的 RS-485/CAN 转接板保持断开，不改变本 Gate。

## 1. 发布定位

本版只讲一条主线：C++20 Linux Runtime 在 ThinkPad `vcan` 上验证协议、监督和故障行为，
在 Orange Pi 4 Pro 上验证 ARM 原生构建、release/systemd 安装合同与普通/FIFO 调度差异。

Orange Pi **默认 stock** 内核仍是 `# CONFIG_CAN is not set`。另有可选 **can1**
档（`6.6.98-sun60iw2-can1`）上跑过 `vcan0 + rcrd` 软件链；那不是默认启动，不是
物理 `can0`，也不能代替 B4 冷启动常驻。本版不声称：

- `rcrd` 已在 Orange Pi 上作为默认服务常驻；
- stock 镜像已完成 SocketCAN/vcan 功能验收；
- can1 软件链等于物理 CAN 或 HAT 已联调；
- 空 callback 的唤醒 lateness 等于 CAN 或控制端到端延迟；
- `SCHED_FIFO` 等于硬实时，软件 EStop 等于功能安全；
- Modbus TCP 双机 demo 等于现场仪表或 Runtime Core 集成。

## 2. 当前证据

| 证据面 | 当前状态 | 对外边界 |
|---|---|---|
| Runtime / daemon | 本工作树默认 **24** 个 CTest 目标 | 正式对外仍要同一 clean commit 重采；4 个目标缺 vcan 会 Skip |
| ThinkPad vcan | 旧 clean 有双进程 7 场与故障矩阵 19/19 | 程序现为 22 场；不能拿旧 19/19 冒充本树 clean |
| Workbench | Phase 3.5 / 4 clean 已关；5A Mock 仅 local | 不是实物执行器，也不是 IPC 隔离 |
| Orange Pi B0/B1 | 主机观察、aarch64 原生构建和非 vcan 测试完成 | 那批证据在 stock、无 CAN |
| Orange Pi B2 | release、manifest、普通用户和 unit 已安装 | stock 上 `rcr-vcan` unsupported，`rcrd` 未 active |
| Orange Pi can1 | 可选内核上 `vcan0 + rcrd` 软件链已跑 | 不是默认启动；不是 `can0`；B4 未关 |
| Orange Pi B3 | 12 格矩阵已有本地 dirty 证据；**RT0 标为 pilot** | 5 秒 Debug、CPU0 小核、空 callback；非正式实时基线 |
| Modbus TCP | localhost 自动化 + Wi-Fi 双机 demo | 不进入 V1 Runtime，不是现场设备证据 |

脱敏、可入库摘要见 [`evidence/portfolio/`](../../evidence/portfolio/README.md)。本地原始样本仍由
`.gitignore` 排除，避免把大样本、主机地址和临时日志无选择地公开。

## 3. 发布 Gate

按顺序关闭；前一项未关，不开始新协议或新硬件分支。

1. **R0 工作树收敛**：完成文档与 Workbench 目录迁移，核对 rename/delete、新路径和
   CMake source list；`git diff --check`、默认构建与 CTest 通过，再形成可审计 commit。
2. **R1 文档一致**：README 只给摘要，本文独占当前状态；SPEC、长期路线和证据摘要对
   `CONFIG_CAN`、can1、dirty/clean 使用同一结论，旧阶段编号只留在 archive。
3. **R2 ThinkPad clean 功能证据**：同一 clean commit 重跑普通 CTest、强制 vcan、
   双进程验收、故障矩阵和 ASan+UBSan；TSan 若仍无法启动只记 `unsupported`。
4. **R3 Orange Pi clean 平台证据**：板上 checkout 同一 clean commit，重跑原生构建、
   非 vcan CTest、12 格矩阵和 release manifest；vcan/rcrd 记 `unsupported/not_run`，不能
   通过 Skip 凑 PASS。
5. **R4 发布摘要**：只纳入脱敏环境字段、矩阵 SUMMARY、四个代表格和 B2 失败边界；
   原始样本用 hash 或本地归档引用，不把内网地址、MAC 和私钥路径公开。

2026-08-11 迁移收口的 dirty-tree local validation：Markdown 本地相对链接检查无缺失，
`git diff --check` 通过；fresh Debug 在 Qt OFF/ON 两种配置均构建成功，两次 CTest 都是
22 `Passed` + 2 `Skipped`（缺少 `vcan0` 的 SocketCAN/Workbench vcan 用例）。这只能说明
目录搬迁和普通构建未被破坏；R0 仍须经有意审查、形成 clean commit 后才能关闭，也不能
据此关闭 R2 的强制 vcan、故障矩阵或 sanitizer Gate。

## 4. B4 与内核停止线

本版允许验证真实 release 之间的 `current` 切换、manifest 和“不删除旧 release”；只有一份
release 时不制造假 ID。stock 内核无 CAN；can1 只证明过手动软件链。冷启动后默认
`rcrd active`、daemon 崩溃重启和新 session Gate（B4）保持开放。

首版不为了全绿而：

- 放宽 `rcrd.service` 对真实 CAN fd 的依赖；
- 增加空转 I/O、FakeCan systemd 服务或通用 Transport；
- 采购 USB-CAN/SPI CAN、EtherCAT 从站或 RS-485；
- 把换内核、PREEMPT_RT 或物理总线塞进本次发布。

若后续确实要求“Orange Pi daemon 常驻”再建立独立的 vcan-capable kernel Gate；它是软件/BSP
实验，不改变 V1 Runtime Core。

## 5. 推荐作品集表述

> 实现 ROS-free C++20 Linux Runtime：使用绝对单调时钟、可观测 FIFO 降级、epoll、
> SocketCAN、watchdog、会话/序号/deadline 和故障监督；在 ThinkPad/vcan 上验证功能闭环，
> 在 Orange Pi 4 Pro 上完成 ARM 原生构建、最小权限 systemd 安装和调度压力对照；
> 默认 stock 内核未启用 CAN，可选 can1 只验证了 `vcan` 软件链，不是物理总线。

投递用展开叙事与简历条见 [`docs/portfolio/`](../portfolio/README.md)。
