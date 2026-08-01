# robot-control-runtime

部署到 Orange Pi Zero 3W 的 ROS-free Linux 边缘运行时。项目用最小系统学习机器人
底层工程：周期调度、fd 事件循环、SocketCAN、watchdog、状态机、trace、benchmark
和 systemd 部署。

V1 不需要新购 CAN 板、传感器、电机或安全器件：ThinkPad 完成开发，Orange Pi
完成 ARM Linux 实机部署，两端都使用 `vcan`。

```text
ThinkPad：开发 / 单测 / 对照 benchmark
                 │ git / ssh / rsync over Wi-Fi（部署流程待实现）
                 ▼
Orange Pi Zero 3W：Linux Runtime / systemd / benchmark
                 │
              SocketCAN
                 │
               vcan0
                 │
          CAN Node Simulator
```

## 当前已实现

- C++20 `rcr` 静态库，零 ROS 2 依赖
- `CLOCK_MONOTONIC` + `clock_nanosleep(TIMER_ABSTIME)` 周期线程
- 可选 `SCHED_FIFO`，支持“必须成功”和“记录失败后降级”两种策略
- `epoll` fd reactor
- Runtime 状态机、软件联锁、命令 watchdog
- latest-wins 普通输出 mailbox
- 命令 session、严格递增 sequence、强制 deadline 校验
- 固定容量 best-effort trace
- SocketCAN、`FakeCanBus`、CAN V1 codec、独立 `rcr_node_sim`、双进程 vcan 验收、CAN 接口只读探测和周期 benchmark
- 可部署 `rcrd`：`eventfd`/`signalfd`、有界输入队列、单节点监督、CAN I/O 线程、有界退出
- 17 个本地测试目标（可选 vcan 场景在缺接口或无权打开 socket 时 Skipped）

已实现 daemon 进程行为与 ThinkPad/`vcan` 验收；**尚未** systemd unit 与 Orange Pi
实测。双进程/daemon 验收需本机已创建 `vcan0`。文档区分“已有能力”和“计划能力”。

## 目录

```text
linux/       独立 CMake 工程：Runtime Core、I/O 与 rcrd
protocol/    已冻结的 CAN V1 线级合同与 golden vectors
firmware/    可选 MCU 实验边界；V1 不构建
docs/        架构、模块原理、部署与多仓边界
evidence/    benchmark / rcrd 验收等可复现证据
```

`linux/include/rcr/` 保持扁平，提供稳定的 `<rcr/...>` include 路径；`linux/src/` 按
`core`、`can`、`linux`、`daemon`、`sim` 分层。目录用于表达实现职责，不额外制造抽象接口。

入口文档：

- [系统规范](SPEC.md)
- [架构](docs/ARCHITECTURE.md)
- [Linux Runtime 原理](docs/LINUX_RUNTIME.md)
- [C/C++、Linux Runtime 与面试知识库](docs/KNOWLEDGE_BASE.md)
- [系统理解图示](docs/images/README.md)
- [最小硬件与可选扩展](docs/HARDWARE_TOPOLOGY.md)
- [姊妹仓边界](docs/SISTER_REPOS.md)
- [当前阶段审计与开发计划](docs/CURRENT_PHASE_PLAN.md)
- [P1–P3 详细执行计划：rcrd、ThinkPad 证据与 Orange Pi 部署](docs/P1_P3_EXECUTION_PLAN.md)
- [`rcrd` 进程合同](docs/RCRD_CONTRACT.md)
- [后续开发路线：EtherCAT、CAN 与 Modbus](docs/DEVELOPMENT_ROADMAP.md)
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

周期线程基准：

```bash
./build/linux/rcr_benchmark --duration-ms 10000 --period-us 1000
```

不要把空回调 benchmark、软件 EStop 或 `vcan` 测试描述成硬实时、功能安全或真实 CAN
台架证据。

## 后续硬件

已有 ESP32-S3 和 STM32F103 都不影响 V1。完成 Orange Pi 部署后，可优先用 ESP32-S3
的板载 USB 做独立诊断/故障注入实验。只有确实需要物理 CAN 波形、错误计数和断线
恢复证据时，再购买一个 Orange Pi CAN 接口和一个 3.3 V MCU CAN 收发器。
