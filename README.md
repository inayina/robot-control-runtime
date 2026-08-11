# robot-control-runtime

部署到 Orange Pi 4 Pro 4GB 的 ROS-free Linux 边缘运行时。项目用最小系统学习机器人
底层工程：周期调度、fd 事件循环、SocketCAN、watchdog、状态机、trace、benchmark
和 systemd 部署。

V1 不需要新购 CAN 板、传感器、电机或安全器件：ThinkPad 用 `vcan` 完成开发与对照；
Orange Pi 承担 ARM Linux 部署与实测。当前厂商镜像未启用 SocketCAN 时，板上以构建、
systemd 安装合同与调度矩阵为证据，**不以** `rcrd`+`vcan` 常驻冒充已关闭。

```text
ThinkPad：开发 / 单测 / 对照 benchmark / Modbus ref server / EtherCAT NIC Gate
                 │ git / ssh / rsync over Wi-Fi
                 ▼
Orange Pi 4 Pro 4GB：SSH、原生构建、systemd 安装合同、ARM benchmark
                 │  （当前镜像无 CONFIG_CAN → 无 vcan / rcrd 未常驻）
              （目标）SocketCAN + vcan0 + rcrd   ← 需带 CAN 的内核后才关闭
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
- 23 个本地测试目标（可选 vcan 场景在缺接口或无权打开 socket 时 Skipped）
- Workbench Phase 3.5 已在 clean commit `cf5892e` 上关闭软件 Gate：23/23 CTest、5/5
  ASan/UBSan、三种 vcan health 结果；见[脱敏摘要](evidence/portfolio/workbench_phase3_5_20260811.md)

已实现 daemon 与 ThinkPad/`vcan` 证据采集路径；审计修复后的正式证据尚待在干净 commit
重采。Orange Pi **P3-A0/A1/A2** 合同与模板已落地；**P3-B 板上实测已部分关闭**：
B0 主机基线、B1 原生构建/`ctest`、B2 release+unit 安装、B3 ARM 12 格（含 sudo FIFO）
有本地证据；脱敏摘要见 [`evidence/portfolio/`](evidence/portfolio/README.md)。厂商内核
`# CONFIG_CAN is not set`，故板上 **无 vcan**，`rcr-vcan`/`rcrd` **未能 active**——安装合同
≠ daemon 常驻。B4 冷启动绿灯与干净 commit 复跑仍开放。Modbus TCP 另有 Wi-Fi 双机
demo（OPi client → ThinkPad `:1502`）。缺 `stress-ng` 时压力格记 `unsupported`。ThinkPad
双进程/daemon/矩阵仍需本机 `vcan0`。

## 目录

```text
linux/       独立 CMake 工程：Runtime Core、I/O 与 rcrd
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
- [后续开发路线：EtherCAT、CAN 与 Modbus](docs/DEVELOPMENT_ROADMAP.md)
- [通信演进边界：CAN、RS485/Modbus RTU 与 EtherCAT](docs/COMMUNICATION_EVOLUTION.md)
- [机器人底层设备测试与诊断工作台开发计划（Phase 4 in progress）](docs/DEVICE_TEST_DIAGNOSTIC_WORKBENCH_DEVELOPMENT_PLAN.md)
- [Optional Qt6 Device Workbench（Phase 4）](docs/QT_DEVICE_WORKBENCH.md)
- [ADR-002：收敛为最小 Linux Runtime](docs/ADR-002-minimal-linux-runtime.md)

## 构建与测试

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

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

Orange Pi 4 Pro 已选定，并已有部分板上证据（构建/安装/ARM 矩阵）；官方 40-pin 未声明
CAN，且当前镜像 `# CONFIG_CAN is not set`，因此 V1 **不能**预设板载 `can0`，也不能把
systemd 安装写成 `rcrd` 已常驻。已有 ESP32-S3 和 STM32F103 都不影响 V1。下一步主线更宜
先关闭[作品集 V1 发布 Gate](docs/PORTFOLIO_V1_RELEASE_PLAN.md)。RS-485/CAN 转接板虽已在途，
P1 保持断开；P1 clean evidence 完成后，才按
[后续权威计划](docs/V1_PHYSICAL_CAN_EXECUTION_PLAN.md) 识别实物并进入内核/DTO/物理 CAN。
[后续权威计划](docs/V1_PHYSICAL_CAN_EXECUTION_PLAN.md)识别实物并进入内核/DTO/物理 CAN。
