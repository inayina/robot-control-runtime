# 零采购作品集 V1 发布计划

状态：Active
冻结日期：2026-08-05
目标：不新增通信实验硬件，形成一版可公开、可复现、证据边界准确的作品集。

## 1. 发布定位

本版只讲一条主线：C++20 Linux Runtime 在 ThinkPad `vcan` 上验证协议、监督和故障行为，
在 Orange Pi 4 Pro 上验证 ARM 原生构建、release/systemd 安装合同与普通/FIFO 调度差异。

Orange Pi 厂商内核 `# CONFIG_CAN is not set`，所以本版不声称：

- `rcrd` 已在 Orange Pi 上常驻；
- Orange Pi 已完成 SocketCAN/vcan 功能验收；
- 空 callback 的唤醒 lateness 等于 CAN 或控制端到端延迟；
- `SCHED_FIFO` 等于硬实时，软件 EStop 等于功能安全；
- Modbus TCP 双机 demo 等于现场仪表或 Runtime Core 集成。

## 2. 当前证据

| 证据面 | 当前状态 | 对外边界 |
|---|---|---|
| Runtime / daemon | 本机构建；18 个测试目标中非 vcan 路径通过 | 当前 ACK/故障重构仍需干净 commit 上重采 vcan Gate |
| ThinkPad vcan | 旧 clean commit 有双进程与故障矩阵 | 不能替代当前未提交实现的正式基线 |
| Orange Pi B0/B1 | 主机观察、aarch64 原生构建和非 vcan 测试完成 | 板卡丝印、供电和完整时钟状态仍不完整 |
| Orange Pi B2 | release、manifest、普通用户和 unit 已安装 | `rcr-vcan` unsupported，`rcrd` 未 active |
| Orange Pi B3 | 12 格矩阵已有本地 dirty 证据；**RT0 标为 pilot** | 5 秒 Debug、CPU0 小核、空 callback；非正式实时基线 |
| Modbus TCP | localhost 自动化 + Wi-Fi 双机 demo | 不进入 V1 Runtime，不是现场设备证据 |

脱敏、可入库摘要见 [`evidence/portfolio/`](../evidence/portfolio/README.md)。本地原始样本仍由
`.gitignore` 排除，避免把大样本、主机地址和临时日志无选择地公开。

## 3. 发布 Gate

按顺序关闭；前一项未关，不开始新协议或新硬件分支。

1. **R0 工作树收敛**：把 Runtime ACK/原子故障、Orange Pi 部署材料、Modbus 实验拆成
   可审计变更；`git diff --check` 通过。
2. **R1 文档一致**：README、SPEC、路线图、本文件和证据摘要对 P3/CONFIG_CAN/dirty
   使用同一结论；历史计划明确归档。
3. **R2 ThinkPad clean 功能证据**：同一 clean commit 重跑普通 CTest、强制 vcan、
   双进程验收、故障矩阵和 ASan+UBSan；TSan 若仍无法启动只记 `unsupported`。
4. **R3 Orange Pi clean 平台证据**：板上 checkout 同一 clean commit，重跑原生构建、
   非 vcan CTest、12 格矩阵和 release manifest；vcan/rcrd 记 `unsupported/not_run`，不能
   通过 Skip 凑 PASS。
5. **R4 发布摘要**：只纳入脱敏环境字段、矩阵 SUMMARY、四个代表格和 B2 失败边界；
   原始样本用 hash 或本地归档引用，不把内网地址、MAC 和私钥路径公开。

## 4. B4 与内核停止线

本版允许验证真实 release 之间的 `current` 切换、manifest 和“不删除旧 release”；只有一份
release 时不制造假 ID。因为当前内核无 CAN，冷启动后 `rcrd active`、daemon 崩溃重启和新
session Gate 保持开放。

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
> 在 Orange Pi 4 Pro 上完成 ARM 原生构建、最小权限 systemd 安装和调度压力对照，并识别
> 厂商内核未启用 CAN 导致的部署阻塞。
