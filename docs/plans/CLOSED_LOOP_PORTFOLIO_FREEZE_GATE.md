# Closed-Loop Portfolio Freeze Gate

状态：**Active**  
授权日期：2026-08-16（用户唯一目标：收敛成可录屏的机器人 Linux Edge Runtime 物理闭环）  
证据目录：[evidence/closed_loop_portfolio/](../../evidence/closed_loop_portfolio/README.md)  
前置（不再扩张）：[Physical Modbus RTU Workbench](PHYSICAL_MODBUS_RTU_WORKBENCH_GATE.md)
最近关闭：Remote Workbench Boundary、Modbus I/O Mock

## 1. 为什么现在做

本仓的核心不是 CAN / Qt / Modbus 技能集合，而是部署在 Orange Pi 上的机器人 Linux
Edge Runtime：设备监督、状态机、watchdog、command freshness、I/O lifecycle 与
fault recovery。CAN 是内部机器人节点链路；Qt 是工程诊断台；Modbus RTU 是低频
Robot Cell 外围 commissioning。

本 Gate 把已有能力收成一条可解释物理闭环，不扩大技术栈：

```text
SG90 TARGET → 对射红外 → STM32 input_bits bit0
  → SocketCAN NodeStatus → NodeSupervisor 快照
  → CellReadyMapper → existing Modbus write_output(0)
  → MR0-IOR08 DO0 requested/confirmed
```

## 2. 演示进程拓扑（不扩张 Remote Workbench）

Orange Pi 上 **一个** 进程拥有 CAN；ThinkPad Qt 只做工程站：

```text
ThinkPad:
  rcr_qt_device_workbench --cell-peer 192.168.1.22:5750 \
      --modbus-peer 192.168.1.22:5740

Orange Pi（先停 rcrd）:
  rcr_modbus_rtu_agent --serial /dev/ttyS7 --listen 0.0.0.0:5740
  rcr_cell_app --can can0 --modbus 127.0.0.1:5740 --listen 0.0.0.0:5750 \
      --evidence physical
        │ RuntimeDaemon / CanIoLoop（唯一 CAN 写者）
        │ CellReadyMapper（关掉 Qt 仍 tick）
        │ localhost TCP → rcr_modbus_rtu_agent → /dev/ttyS7 → MR0 DO0
CEL1 magic，不是 Modbus RCRM，也不是 Remote HELLO。
不建物理 PC–ARM Runtime remote 产品，也不并行再跑 rcrd。
```

ThinkPad 继续承担开发、单测、vcan 对照。不得把本机 Qt `--can can0` 写成当前演示拓扑。

## 3. 冻结合同

| 名称 | 拥有者 | 禁止 |
|---|---|---|
| `POSITION_REACHED` | STM32 `NodeStatus.input_bits` bit0 | 新 CAN 消息、fault_code/flags、LIGHT_ON |
| `CellReady` | `linux/src/workbench/application` `CellReadyMapper` | Runtime Core、decoder、STM32 |
| DO0 | `PhysicalModbusIoService.write_output(0)` requested≠confirmed | 把 Modbus 放进 `rcrd` |

`CellReady = device.online AND mode==Active AND (input_bits&1) AND fault_code==0`。
false→true 请求 DO0 ON；true→false 或 Fault/Hold 请求 OFF。重连不自动重放。

物理 Modbus backend 已作为前置存在；本 Gate 只把它接到闭环，不新增多从站、图表或
通用总线框架。

## 4. 停止线

- EtherCAT、PREEMPT_RT、ROS 2、新 Dashboard、ITransport、FieldbusManager
- 把 CAN 与 Modbus 合成一套协议、真实伺服框架、新仓库
- 为美观重构 Runtime Core、扩张 Remote Workbench、市电/大功率负载
- 静默 Mock 回退；把未采集的实物写成 PASS
- 同时再开第二个 Current Gate

## 5. 关闭条件

软件路径：bit0 语义冻结、监督器暴露 `input_bits`、STM32 去抖、CellReadyMapper、
Qt Overview/Runtime/Cell I/O/Verification、本仓 Linux CTest 不回退。

实物关闭必须按 [evidence/closed_loop_portfolio](../../evidence/closed_loop_portfolio/README.md)
的 15 项采集；缺项保持 **NOT RUN**，不得标 PASS。极性已按遮挡=HIGH 冻结为 ACTIVE_HIGH；
无遮挡为推断 LOW。红外边沿与 CAN bit0 仍须上总线观察。
