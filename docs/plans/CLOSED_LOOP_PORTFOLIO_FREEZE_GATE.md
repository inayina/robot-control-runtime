# Closed-Loop Portfolio Freeze Gate

状态：**Deferred / still open**（不得标 CLOSED）。2026-08-18 用户选择 B 后，唯一
Current Gate 改为
[Post-Audit Local Development SPEC](POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md)。
本文仍记录实物闭环缺项，不删除、不降级、不预填。第 1–10、12–13 项见
[evidence/closed_loop_portfolio](../../evidence/closed_loop_portfolio/README.md)。
第 11 项 RS-485 掉线瞬间仍缺；无运动录像。  
授权日期：2026-08-16  
推迟日期：2026-08-18  
证据目录：[evidence/closed_loop_portfolio/](../../evidence/closed_loop_portfolio/README.md)  
前置（不再扩张）：[Physical Modbus RTU Workbench](PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md)  
最近关闭：Remote Workbench Boundary、Modbus I/O Mock

**功能冻结：** Portfolio V1 不再开新能力。EtherCAT / ROS 2 / PREEMPT_RT / 新 UI / 新总线
一律不做。本 Gate 重新成为 Current 之前，只保留已有实物证据状态。

## 1. 为什么现在做

本仓的核心不是 CAN / Qt / Modbus 技能集合，而是部署在 Orange Pi 上的机器人 Linux
Edge Runtime：设备监督、状态机、watchdog、command freshness、I/O lifecycle 与
fault recovery。CAN 是内部机器人节点链路；Qt 是工程诊断台；Modbus RTU 是低频
Robot Cell 外围 commissioning。

本 Gate 把已有能力收成一条可解释物理闭环，不扩大技术栈：

```text
SG90 TARGET → 对射红外 PA0 → STM32 input_bits bit0
  → SocketCAN NodeStatus → NodeSupervisor 快照
  → CellReadyMapper → existing Modbus write_output(0)
  → MR0-IOR08 DO0 requested/confirmed
```

无外接单元灯。主图见 [ARCHITECTURE.md](../ARCHITECTURE.md)。

## 2. 演示进程拓扑

Orange Pi 上 **一个** 进程拥有 CAN；ThinkPad Qt 只做工程站：

```text
ThinkPad:
  rcr_qt_device_workbench --cell-peer 192.168.1.22:5750
  （DO0 只读 CEL1；不要再加 --modbus-peer 去抢 agent）

Orange Pi（先停 rcrd）:
  rcr_modbus_rtu_agent --serial /dev/ttyS7 --listen 0.0.0.0:5740
  rcr_cell_app --can can0 --modbus 127.0.0.1:5740 --listen 0.0.0.0:5750 \
      --evidence physical
```

`rcrd` 与 `rcr_cell_app` 共用 `RuntimeDaemon`；主 Demo 只跑 `rcr_cell_app`。
CEL1 不是 Modbus RCRM，也不是 Remote HELLO。

## 3. 冻结合同

| 名称 | 拥有者 | 禁止 |
|---|---|---|
| `POSITION_REACHED` | STM32 `NodeStatus.input_bits` bit0（PA0） | 新 CAN 消息、LIGHT_ON、改 GPIO |
| `CellReady` | `CellReadyMapper` | Runtime Core、decoder、STM32、Qt |
| DO0 自动闭环 | 边缘 mapper → agent FC05 | Qt `--cell-peer` 当第二 owner |

```text
CellReady = device.online
          AND mode == Active
          AND POSITION_REACHED
          AND device_fault_code == 0
          AND Runtime fault == None
```

false→true 请求 DO0 ON；true→false 或 Fault/Hold 请求 OFF。重连不自动重放。

## 4. 停止线

- EtherCAT、PREEMPT_RT、ROS 2、新 Dashboard、ITransport、FieldbusManager
- 把 CAN 与 Modbus 合成一套协议、真实伺服框架、新仓库
- 为美观重构 Runtime Core、扩张 Remote Workbench
- 静默 Mock 回退；把未采集的实物写成 PASS
- 因软件 CTest 全过而关闭本 Gate

## 5. 关闭条件

软件：bit0 语义冻结、监督器暴露 `input_bits`、STM32 去抖、CellReadyMapper、
Qt 四页、`--cell-peer` DO0 只读 CEL1、Linux CTest 不回退。

实物：必须按 [evidence/closed_loop_portfolio](../../evidence/closed_loop_portfolio/README.md)
的 **13 项** 采集；缺项保持未跑。极性 `ACTIVE_HIGH`（遮挡=HIGH）。
外接 LED 不是关闭条件。2026-08-16 已过第 1–10、12–13 项；第 11 项 RS-485 掉线瞬间仍未跑。

## FINAL MANUAL CHECKLIST

用户在台架上跑完并保存证据后，才允许把本文改为 CLOSED / PASS。

1. Orange Pi：停 `rcrd`；起 agent + `rcr_cell_app --evidence physical`；记录 `uname` 与 `can0`。
2. ThinkPad：`rcr_qt_device_workbench --cell-peer <orangepi>:5750`（不要 `--modbus-peer`）。
3. Overview 空闲态可讲清：Runtime / Node / Cell Ready / DO0。
4. Activate → Command HOME → 目视 SG90 → Command TARGET。
5. 挡片进对射槽：PA0 变化 → candump bit0 → Overview REACHED → CellReady TRUE → DO0 CONFIRMED ON。
6. 截图 Overview；录 45–90 s。
7. 拔 RS-485：Cell I/O OFFLINE/TIMEOUT；Qt 仍可用；接回后 Probe；确认不重放旧 DO0。
8. 记录 `git rev-parse HEAD` 与 dirty/clean；把日志/截图/录像放进 evidence 目录。

不要为 Demo 新造 Runtime 故障策略。CAN heartbeat loss 走既有 CommLoss。
