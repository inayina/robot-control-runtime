# 机器人边缘 Runtime 与设备工程站

**Portfolio V1：FUNCTION FROZEN。** LD0–LD8 已关闭，当前没有 Active Development Gate；
Closed-Loop Physical Acceptance 保持 `OPEN / DEFERRED`。仓用途是面试复习、bug fix 和 evidence
复现。实物闭环证据以
[closed_loop_portfolio](evidence/closed_loop_portfolio/README.md) 的 13 项为准。
不得因软件测试关闭该 Gate。

## 为什么做这个项目

前序项目已经能做设备控制和 ROS 2 任务执行。本仓补的是长期运行还缺的那一层：
设备监督、命令新鲜度、watchdog、状态迁移、故障处理和可预期的 Linux I/O 生命周期。
不再重复电机 PID、编码器或 MCU RTOS。

## 它是什么

一套不依赖 ROS 的 Linux 边缘 Runtime，部署在 Orange Pi 上：经 SocketCAN 连 STM32
机器人节点，经 Modbus RTU 连单元远程 I/O。ThinkPad 上的 Qt 工程站只做观察和下发，
不拥有现场总线。

## 结果

软件路径已经把因果链接上：SG90 运动与对射到位 → CAN `POSITION_REACHED` →
Runtime/应用层 `CellReady` → MR0-IOR08 DO0 的 requested / confirmed。Qt 只观察这条链。

**物理闭环表未整表通过。** 2026-08-16 已有 Overview 成功态截图（REACHED / CellReady / DO0 CONFIRMED）；无运动录像，RS-485 掉线瞬间仍缺。

## 系统架构

```text
                        ThinkPad
               ┌─────────────────────┐
               │ Qt 工程站            │
               │ 观察 / 下发          │
               └──────────┬──────────┘
                          │ CEL1 / TCP
──────────────────────────┼────────────────────
                          ▼       Orange Pi
                ┌─────────────────────┐
                │    rcr_cell_app     │
                │  RuntimeDaemon      │
                │  CellReadyMapper    │
                └────┬───────────┬────┘
                     │           │
                 SocketCAN   localhost TCP
                     │           │
                     ▼           ▼
                STM32F103   rcr_modbus_rtu_agent
                 │     │         │ /dev/ttyS7
               SG90   PA0        │ Modbus RTU
                      红外        ▼
                              MR0-IOR08 DO0
```

`rcrd` 是同一套 `RuntimeDaemon` 的独立宿主（vcan、systemd、CLI）。作品集演示进程是
`rcr_cell_app`。不要两个进程同时写 `can0`。

职责边界见 [ARCHITECTURE.md](docs/ARCHITECTURE.md) 与
[CODE_OWNERSHIP_MAP.md](docs/CODE_OWNERSHIP_MAP.md)。

## 物理闭环

```text
Qt Activate / HOME / TARGET
        ↓ CEL1
rcr_cell_app → Runtime 命令准入 → SocketCAN
        ↓
STM32：PA8 SG90 PWM，PA0 TARGET_SENSOR_DO
        ↓
POSITION_REACHED（NodeStatus.input_bits bit0）
        ↓
CellReadyMapper（边缘应用策略）
        ↓
Modbus RTU FC05 → MR0 DO0 requested / confirmed
        ↓
Qt Overview（只读）
```

引脚已冻结：`PA8` 舵机 PWM，`PA0` 到位传感器，`PA11/PA12` CAN。极性：遮挡 = PA0 高电平
（`ACTIVE_HIGH`）。本演示没有外接单元灯。

## Runtime 设计

`RuntimeDaemon` 拥有运行时状态、watchdog、设备监督、命令准入（session / sequence /
deadline）、故障恢复、调度器和 SocketCAN 生命周期。

设备能动之后，这一层保证控制路径可监督、可准入、可恢复。

CAN（机器人节点）和 Modbus RTU（单元 I/O）是两条总线、两套故障语义，不是同一套
Transport。

## 工程站

Qt `--cell-peer` 只观察和下发工程命令：

- Activate Runtime，下发 HOME / TARGET
- 查看 Runtime / CAN 节点 / `POSITION_REACHED` / CellReady / MR0 DO0
  （requested 与 confirmed 分开）
- 验证（CAN Health、事件、evidence 路径）

它不拥有 CAN fd、watchdog 策略、CellReady、DO0 自动闭环或安全功能。

默认页：Overview、Runtime、Cell I/O、Verification。实验页留在代码里，需要时加
`--show-lab`。

## 验证

| 类型 | 证明什么 | 状态 |
|---|---|---|
| Linux / Qt / STM32 主机 CTest | 软件合同 | 以本树实测为准；见冻结 Gate 的测试结果 |
| Orange Pi SSH / 原生构建 / ARM 调度 | stock Linux 上的部署 | 见 `evidence/orangepi*` |
| STM32 双向 CAN、SG90 目视、PC13 | 独立物理 CAN smoke | `evidence/stm32f103_can/`（dirty-tree，不是本 Gate） |
| 闭环作品集表 | SG90 → PA0 → CellReady → MR0 DO0 → Qt | 部分采集，未关闭 |

软件 PASS 不能升格成实物 PASS。

## 构建与运行

默认（Runtime，不含 Qt）：

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

显式打开 Qt 工程站：

```bash
cmake -S linux -B build/qt-on -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON
cmake --build build/qt-on -j2
ctest --test-dir build/qt-on --output-on-failure
```

ThinkPad `vcan`（软件对照，不是物理演示）：

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
./build/linux/rcr_node_sim --can vcan0 --node-id 1 --duration-ms 2000
./build/linux/rcrd --can vcan0 --node-id 1 --duration-ms 1000
```

物理演示（Orange Pi 拥有 CAN；先停板上 `rcrd`）：

```bash
rcr_modbus_rtu_agent --serial /dev/ttyS7 --listen 0.0.0.0:5740
rcr_cell_app --can can0 --modbus 127.0.0.1:5740 --listen 0.0.0.0:5750 \
  --evidence physical
```

ThinkPad：

```bash
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --cell-peer 192.168.1.22:5750
```

STM32 主机逻辑测试在 `firmware/stm32f103/`，不由 `linux/` CMake 构建。

## 限制

- SG90 是低风险演示件，不是工业伺服。
- PA0 是离散光电到位，不是编码器反馈。
- CAN V1 是本项目协议。
- MR0 DO0 证明物理远程 I/O（`requested != confirmed`）；没有外接 LED。
- 不声称功能安全、硬实时、PREEMPT_RT、EtherCAT、ROS 2 集成或量产控制器。
- `SCHED_FIFO` 是 stock Linux 上的 POSIX 调度策略，不等于 RTOS。

这些是证据边界，不是下一步要补的功能。

**冻结，不要启动：** EtherCAT、ROS 2、PREEMPT_RT、新 UI、新现场总线、更多执行器、
多节点 CAN、更多 Modbus 设备、Web Dashboard、插件/Transport 框架、大重构。

## 仓库地图

| 路径 | 职责 |
|---|---|
| `linux/` | C++20 Runtime、`rcrd`、`rcr_cell_app`、测试、可选 Qt |
| `protocol/` | 已冻结的 CAN V1 线级合同 |
| `firmware/stm32f103/` | 独立 CAN 节点（PA8 / PA0 / bxCAN） |
| `deploy/` | Orange Pi 发布 / systemd / 上电 |
| `evidence/` | 可复现结果；目录名不等于 PASS |
| `docs/` | 架构、所有权、工程站、作品集 |
| `experiments/` | 历史 / 实验（EtherCAT、额外 Modbus、实时） |

| 接着读 | 文件 |
|---|---|
| 一页讲稿 | [docs/PORTFOLIO_SUMMARY.md](docs/PORTFOLIO_SUMMARY.md) |
| 架构 | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| 范围 | [SPEC.md](SPEC.md) |
| 所有权 | [docs/CODE_OWNERSHIP_MAP.md](docs/CODE_OWNERSHIP_MAP.md) |
| 当前 Gate | [docs/plans/POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md](docs/plans/POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md) |
| 证据 | [evidence/README.md](evidence/README.md) |
| 面试 | [docs/KNOWLEDGE_BASE.md](docs/KNOWLEDGE_BASE.md) |
