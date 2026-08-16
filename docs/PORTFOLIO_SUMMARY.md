# Portfolio V1 摘要

**机器人边缘 Runtime 与设备工程站**  
状态：功能冻结。物理闭环 Gate 仍为 Active / 部分采集，未关闭。

## 一句话问题

设备能动之后，谁保证它在 Linux 上长期、可监督、可恢复地跑？本仓补这一层，而不是再做一套 PID 或 ROS 2 任务栈。

## 系统架构

ThinkPad Qt（观察/下发）→ CEL1 → Orange Pi `rcr_cell_app`（`RuntimeDaemon` + `CellReadyMapper`）→ SocketCAN → STM32（SG90 + PA0）；同时经本机 Modbus agent → MR0 DO0。

`rcrd` 是同一个 `RuntimeDaemon` 的独立宿主，不是第二套 Runtime。物理 `can0` 同一时刻只有一个写者。

## 三个核心工程点

1. **Runtime**：状态机、watchdog、设备监督、session/sequence/deadline、故障恢复、epoll / SocketCAN 生命周期。
2. **物理节点**：STM32F103 CAN 节点；PA8 两档 SG90；PA0 对射确认 `POSITION_REACHED`（不是编码器）。
3. **单元外围**：`CellReady` 应用策略映射到 MR0 DO0；`requested != confirmed`；RS-485 丢失不静默重放。

## 物理演示

目标因果链：HOME/TARGET → SG90 运动 → 挡片进对射槽 → PA0 → CAN bit0 → CellReady → FC05 → MR0 DO0 confirmed → Qt Overview。

无外接单元灯。软件路径已接线；本表实物项见 [evidence/closed_loop_portfolio](../evidence/closed_loop_portfolio/README.md)，缺日志则未跑。

## 验证结果

- Linux / Workbench / CEL1 / Modbus / Qt offscreen / STM32 主机逻辑：软件合同，以当时 CTest 为准。
- Orange Pi 构建/调度、STM32 双向 CAN 与 SG90 目视：独立历史 evidence，不是本 Gate 通过。
- 闭环 13 项：2026-08-16 已过环境到 Overview 成功态；无运动录像；RS-485 掉线瞬间仍缺，不得标整表通过。

## 限制

SG90 不是工业伺服；光电是离散到位；CAN V1 是项目协议；不声称功能安全、硬实时、PREEMPT_RT、EtherCAT、ROS 2 或量产。

## 面试讲法

30 秒：前序项目已能控设备和跑任务；本仓做 Orange Pi 上不依赖 ROS 的 Runtime——监督、命令新鲜度、watchdog、故障恢复。物理演示把 SG90 运动经 PA0/CAN 变成 CellReady，再确认 MR0 DO0。Qt 只是工程站。

3–5 分钟：架构图 → `RuntimeDaemon` 与 `rcr_cell_app`/`rcrd` → CAN 命令准入 → STM32/PA0 → CellReady 不进 Core → Modbus agent 只做物理 I/O → Qt `--cell-peer` 只读 CEL1 → CAN 丢线 ≠ RS-485 丢线 → 证据边界。

## 作品集四件套（用户采集）

1. 架构图：本文 / README / `docs/ARCHITECTURE.md` 主图。
2. Qt Overview 截图：`evidence/portfolio/qt_overview_cell_ready_20260816.png`（ACTIVE / ONLINE / REACHED / CellReady TRUE / DO0 CONFIRMED ON）。
3. 45–90 秒录像：HOME → TARGET → 红外 → DO0。
4. 故障一段：拔 RS-485 → OFFLINE → Probe 恢复，或不插心跳 → CommLoss。勿为作品集再扩故障矩阵。
