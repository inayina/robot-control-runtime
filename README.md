# robot-control-runtime

部署到 Orange Pi 4 Pro 4GB 的 ROS-free Linux 边缘运行时。项目用最小系统学习机器人
底层工程：周期调度、fd 事件循环、SocketCAN、watchdog、状态机、trace、benchmark
和 systemd 部署。

V1 不需要新购 CAN 板、传感器、电机或安全器件：ThinkPad 用 `vcan` 完成开发与对照；
Orange Pi 承担 ARM Linux 部署与实测。板上默认库存内核仍未启用 SocketCAN，但已经保留并
验证过可选的 CAN 档 1 内核；两者不能混成同一运行状态，也不能把 `vcan` 写成物理 CAN。

```text
ThinkPad：开发 / 单测 / 对照 benchmark / Modbus ref server / EtherCAT NIC Gate
                 │ git / ssh / rsync over Wi-Fi
                 ▼
Orange Pi 4 Pro 4GB：SSH、原生构建、systemd 安装合同、ARM benchmark
                 ├─ 默认 stock：6.6.98-sun60iw2，CONFIG_CAN 未启用
                 └─ 可选 can1：6.6.98-sun60iw2-can1，vcan0 + rcrd 软件链已跑
              （仍开放）HAT / MCP2515 / physical can0 → 需要档 2 与设备树联调
```

## 当前已实现

- C++20 `rcr` 静态库，零 ROS 2 依赖
- `CLOCK_MONOTONIC` + `clock_nanosleep(TIMER_ABSTIME)` 周期线程
- 可选 `SCHED_FIFO`，支持“必须成功”和“记录失败后降级”两种策略
- `epoll` fd reactor
- Runtime 状态机、软件联锁、命令 watchdog
- latest-wins 普通输出 mailbox
- 命令 session、严格递增 sequence、强制 deadline 校验
- 单锁 `raise_fault` 故障升级；单笔在途 OutputStatus 匹配、ACK 超时 Hold 与可观测计数
- 固定容量 best-effort trace
- SocketCAN、`FakeCanBus`、CAN V1 codec、独立 `rcr_node_sim`、双进程 vcan 验收、CAN 接口只读探测和周期 benchmark
- 可部署 `rcrd`：`eventfd`/`signalfd`、有界输入队列、单节点监督、CAN I/O 线程、有界退出
- ThinkPad 证据基线：ASan+UBSan 脚本、TSan（环境不支持则记 `unsupported`）、自动故障矩阵、
  唤醒 lateness 分位数与 12 组调度/负载矩阵脚本
- 24 个本地测试目标（可选 vcan 场景在缺接口或无权打开 socket 时 Skipped）
- Workbench Phase 3.5 已在 clean commit `cf5892e` 上关闭软件 Gate：23/23 CTest、5/5
  ASan/UBSan、三种 vcan health 结果；见[脱敏摘要](evidence/portfolio/workbench_phase3_5_20260811.md)
- 可选 Qt6 Device Workbench：Overview、CAN Health、Diagnostics、Results；Phase 4 已在 clean
  commit `834ec89` 上完成 Qt OFF/ON 23/23 与 offscreen VCAN Gate；见
  [Phase 4 摘要](evidence/portfolio/qt_workbench_phase4_20260811.md)
- Phase 5A `Actuator 01` 隔离 Mock：Enable、Homing、velocity、Jog deadman、Normal/Quick Stop、
  soft limit、tracking error 与 Fault Reset；当前仅为 dirty-tree local validation，不是实物执行器
- Orange Pi CAN 内核档 1：`6.6.98-sun60iw2-can1` 已产出，包含 `CAN/RAW/DEV/VCAN` 模块；
  采用 stock/can1 双启动与串口回退，can1 下已跑过 `vcan0 + rcrd` 软件链

已实现 daemon 与 ThinkPad/`vcan` 证据采集路径；审计修复后的正式证据尚待在干净 commit
重采。Orange Pi **P3-A0/A1/A2** 合同与模板已落地；**P3-B 板上实测已部分关闭**：
B0 主机基线、B1 原生构建/`ctest`、B2 release+unit 安装、B3 ARM 12 格（含 sudo FIFO）
有本地证据；脱敏摘要见 [`evidence/portfolio/`](evidence/portfolio/README.md)。当时采集 B0–B3
证据所用的库存内核是 `# CONFIG_CAN is not set`，所以那批证据里的 `rcr-vcan`/`rcrd`
inactive 结论仍然有效。后续另行完成的 can1 档 1 已补上板上 `vcan0 + rcrd` 软件链，但不
追溯改写旧证据，也不自动证明 systemd 冷启动常驻 Gate。B4 冷启动绿灯与干净 commit 复跑仍
开放。Modbus TCP 另有 Wi-Fi 双机 demo（OPi client → ThinkPad `:1502`）。缺 `stress-ng`
时压力格记 `unsupported`。

## 目录

```text
linux/       独立 CMake 工程：Runtime Core、I/O、rcrd、headless Workbench 与可选 Qt 工具
protocol/    已冻结的 CAN V1 线级合同与 golden vectors
firmware/    可选 MCU 实验边界；V1 不构建
experiments/ 独立协议/集成实验；不由 linux/ 递归构建，不自动进入 rcrd
deploy/      Orange Pi 部署：release 布局 + systemd unit + bring-up 勾选表（P3-A0/A1/A2）
docs/        架构、模块原理、部署与多仓边界
evidence/    benchmark / rcrd 验收等可复现证据
```

`linux/include/rcr/` 保持扁平，提供稳定的 `<rcr/...>` include 路径；`linux/src/` 按
`core`、`can`、`linux`、`runtime`、`supervision`、`daemon`、`sim` 归类。目录表达职责
归属，不代表严格单向依赖，也不为目录整齐额外制造抽象接口。

入口文档：

- [系统规范](SPEC.md)
- [架构](docs/ARCHITECTURE.md)
- [“五层一横”架构与 A–G 证据路线](docs/FIVE_LAYERS_ONE_PLANE.md)
- [Linux Runtime 原理](docs/LINUX_RUNTIME.md)
- [C/C++、Linux Runtime 与面试知识库](docs/KNOWLEDGE_BASE.md)
- [全项目模块知识卡](docs/MODULE_KNOWLEDGE_CARDS.md)
- [系统理解图示](docs/images/README.md)
- [最小硬件与可选扩展](docs/HARDWARE_TOPOLOGY.md)
- [姊妹仓边界](docs/SISTER_REPOS.md)
- [底层系统软件作品集（投递入口）](docs/portfolio/README.md)
- [七仓能力链总图（作品集叙事；不是已合并部署拓扑）](docs/portfolio/assets/seven_repo_capability_chain.svg)
- [MCU → Dashboard 数据流图（代码/合同视图）](docs/portfolio/assets/digital_twin_end_to_end_dataflow.svg)
- [零采购作品集 V1 发布计划](docs/PORTFOLIO_V1_RELEASE_PLAN.md)
- [Orange Pi Real-time Linux 学习与 PREEMPT_RT 对照计划](docs/REALTIME_LINUX_LEARNING_PLAN.md)
- [Real-time Linux 证据 Schema（RT0）](docs/REALTIME_EVIDENCE_SCHEMA.md)
- [RT3 用户态实时编程夹具](experiments/realtime_userspace/README.md)
- [历史阶段审计（已归档）](docs/CURRENT_PHASE_PLAN.md)
- [P1–P3 详细执行计划：rcrd、ThinkPad 证据与 Orange Pi 部署](docs/P1_P3_EXECUTION_PLAN.md)
- [后续权威计划：V1 收口 → BSP/Physical CAN → 真总线 Runtime](docs/V1_PHYSICAL_CAN_EXECUTION_PLAN.md)
- [证据 Schema（P2）](docs/EVIDENCE_SCHEMA.md)
- [`rcrd` 进程合同](docs/RCRD_CONTRACT.md)
- [Orange Pi bring-up 与部署合同（P3-A0；A2 勾选表见 `deploy/orangepi/BRINGUP_CHECKLIST.md`）](docs/ORANGE_PI_BRINGUP.md)
- [Orange Pi CAN 内核档 1：双启动、回退与 vcan 软件链](docs/ORANGE_PI_CONFIG_CAN_PLAN.md)
- [Orange Pi CAN 内核证据索引](evidence/orangepi_can_kernel/README.md)
- [后续开发路线：EtherCAT、CAN 与 Modbus](docs/DEVELOPMENT_ROADMAP.md)
- [通信演进边界：CAN、RS485/Modbus RTU 与 EtherCAT](docs/COMMUNICATION_EVOLUTION.md)
- [机器人底层设备测试与诊断工作台开发计划（Phase 5A Mock local pass）](docs/DEVICE_TEST_DIAGNOSTIC_WORKBENCH_DEVELOPMENT_PLAN.md)
- [Optional Qt6 Device Workbench（Phase 5A Mock local pass）](docs/QT_DEVICE_WORKBENCH.md)
- [Actuator 01 Commissioning Profile（A0/A1/A3 local；A2 open）](docs/ACTUATOR_COMMISSIONING_PROFILE_PLAN.md)
- [Qt 零基础对照本仓笔记](docs/QT_WORKBENCH_NOTES.md)
- [ADR-002：收敛为最小 Linux Runtime](docs/ADR-002-minimal-linux-runtime.md)

## 构建与测试

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

默认构建不查找或链接 Qt。启用可选 Workbench：

```bash
cmake -S linux -B build/qt-on \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON
cmake --build build/qt-on -j2
ctest --test-dir build/qt-on --output-on-failure
```

## Qt Device Workbench 与 Actuator 01

Workbench 是本地机器人设备 commissioning / diagnostics 工具，不是 Web Dashboard、ROS 2
HOC 或 CNC controller。依赖与控制方向为：

```text
MainWindow（presentation）
    ↓ signal / slot
WorkbenchController
    ├─ RuntimeApplicationAdapter → RuntimeDaemon → SocketCAN
    └─ MockActuatorProfile（MOCK / ISOLATED）
```

- `MainWindow` 不拥有 Runtime 状态机、watchdog、fault manager 或 CAN fd；
- 100 ms `QTimer` 只刷新 snapshot；CAN Health/结果写入位于 worker thread；
- Actuator Mock 由 10 ms UI timer 显式推进，但不是 realtime control；
- Jog 使用 generation token、200 ms deadman 和 2 s 最大连续时长；
- `QUICK STOP` 是软件快速减速，不是硬件 E-stop、STO 或功能安全证明；
- 现有 ordinary digital-output mailbox 没有被拿来伪装 Home/Jog/velocity command。

启动 UI（需要已有 SocketCAN 接口；Actuator 页本身不发送 motion CAN frame）：

```bash
./build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --results workbench-results
```

无显示服务器的 Actuator 纵向 smoke：

```bash
QT_QPA_PLATFORM=offscreen \
./build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --run-actuator-smoke-once
```

当前 Phase 5A local 结果：Qt OFF 24/24、Qt ON 24/24、Workbench ASan/UBSan 6/6，
`Enable → Home → Start → Quick Stop → READY` smoke 为 `pass`，Runtime TX 为 0。由于 Phase 5
开始前工作树已有未提交文档修改，这些只能作为 dirty-tree local evidence，尚未形成正式 clean
baseline。A2 Runtime command admission、actuator CAN wire contract、真实 MCU/servo 和进程级
Qt crash isolation 均未实现。

## Runtime / vcan 与证据工具

可选创建 `vcan0`（需要相应系统权限；库代码不会替你创建接口）：

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
```

```bash
# 可部署 daemon（需已有 vcan0；不自动发演示输出）
./build/linux/rcrd --can vcan0 --node-id 1 --duration-ms 1000
```

可选 SocketCAN 回环与 daemon 场景在缺少 `vcan0` 或无权打开 CAN socket 时会显示
`Skipped`，而不是假 PASS。阶段验收（缺失即失败）需显式运行：

```bash
./build/linux/tests/test_socketcan_vcan --require-vcan
```

独立节点模拟器（需先创建 `vcan0`）：

```bash
./build/linux/rcr_node_sim --can vcan0 --node-id 1 --duration-ms 2000
```

故障注入参数默认关闭，例如 `--fault-stop-heartbeat`、`--fault-restart-after-ms 500`。

双进程阶段验收（缺接口即失败，结果写入 `evidence/vcan_acceptance/`）：

```bash
./linux/scripts/run_vcan_acceptance.sh vcan0
```

周期线程基准（空 callback 测唤醒 lateness；可选受控过载）：

```bash
./build/linux/rcr_benchmark --duration-ms 10000 --period-us 1000 --samples-out /tmp/samples.txt
# A-T：1 ms 周期 + 3 ms callback；观察 miss 增长且 cycles << duration/period
./build/linux/rcr_benchmark --duration-ms 200 --period-us 1000 --callback-delay-us 3000
```

ThinkPad P2 证据入口（schema 见 `docs/EVIDENCE_SCHEMA.md`）：

```bash
./linux/scripts/run_asan_ubsan.sh
./linux/scripts/run_tsan.sh
./linux/scripts/run_fault_matrix.sh vcan0
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_thinkpad_benchmark_matrix.sh
```

不要把空回调 benchmark、软件 EStop 或 `vcan` 测试描述成硬实时、功能安全或真实 CAN
台架证据。

可选的 CAN + Modbus 类型化观测实验位于
[`experiments/multibus_observer/`](experiments/multibus_observer/README.md)。它用独立 CAN
事件线程与低速 Modbus 事务线程汇聚只读快照，不改变 V1 Runtime，也不把两种协议强塞进
一个通用 `IBus`。观测快照与 `rcrd` 命令路径的未来接点（Deferred）见
[`docs/OBSERVATION_TO_EXECUTION_CONTRACT.md`](docs/OBSERVATION_TO_EXECUTION_CONTRACT.md)。

## 后续硬件

Orange Pi 4 Pro 已选定，并已有构建、安装、ARM 矩阵和 CAN 内核档 1 证据。当前启动合同是：

- `stock`：默认回退内核 `6.6.98-sun60iw2`，`# CONFIG_CAN is not set`；
- `can1`：可选 `6.6.98-sun60iw2-can1`，包含 `CONFIG_CAN/RAW/DEV/VCAN=m`，已验证
  `vcan0 + rcrd` 软件链；
- 通过 `kernel_flavor=stock|can1` 选择，首次切换必须有 USB-TTL 回退；禁止覆盖唯一 stock
  `uImage`，也禁止对已挂载根分区使用 `debugfs -w`。

官方 40-pin 未声明 CAN，档 1 也未包含已经验证的 MCP2515/overlay/physical `can0`，因此仍
不能把板上写成物理 CAN 或 HAT 已联调。已有 ESP32-S3 和 STM32F103 都不影响 V1。下一步
主线更宜先关闭[作品集 V1 发布 Gate](docs/PORTFOLIO_V1_RELEASE_PLAN.md)。RS-485/CAN 转接板
虽已在途，P1 保持断开；P1 clean evidence 完成后，才按
[后续权威计划](docs/V1_PHYSICAL_CAN_EXECUTION_PLAN.md) 识别实物并进入内核/DTO/物理 CAN。
