# robot-control-runtime

面向 Orange Pi 4 Pro 4GB 的 ROS-free C++20 Linux Edge Runtime。项目聚焦机器人底层
系统岗位需要的周期调度、Linux fd、SocketCAN、状态监督、可观测性和部署，不重复其他
仓库已经完成的 MCU 电机闭环。

```text
ThinkPad
  ├─ vcan 功能闭环、单测、sanitizer、故障矩阵
  └─ benchmark 对照与开发工作流
            │ git / ssh
            ▼
Orange Pi 4 Pro 4GB
  ├─ ARM 原生构建、release/systemd 安装、调度测量
  ├─ stock：CONFIG_CAN 未启用
  └─ can1：跑过 vcan0 + rcrd 软件链；不是 physical can0
```

## 当前主线

唯一当前执行入口是
[作品集 V1 发布 Gate](docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md)：先收敛工作树，再在同一
clean commit 上重采 ThinkPad 与 Orange Pi 证据。Workbench A2、物理 CAN、EtherCAT、
Modbus 扩展和 PREEMPT_RT 均不与本轮并行推进。

## 已实现

**Runtime**

- `CLOCK_MONOTONIC` 绝对周期调度与可观测 `SCHED_FIFO` 降级；
- Runtime 状态机、软件联锁、watchdog、latest-wins 普通输出邮箱；
- session、sequence、deadline、原子故障升级、单笔在途 ACK 与输出 lease；
- `epoll`、SocketCAN、`eventfd`/`signalfd`、有界输入队列和有界关闭；
- CAN V1 codec、独立节点模拟器、双进程 vcan 验收和故障矩阵。

**部署与证据**

- ThinkPad 单测、ASan+UBSan、TSan 分类、故障矩阵和调度矩阵采集路径；
- Orange Pi 原生构建、release/current/manifest、systemd unit 和 ARM 调度测量；
- Orange Pi stock 与 can1 内核证据分开记录，不把 vcan 写成物理 CAN；
- 结果使用 `pass`、`failed`、`permission_denied`、`unsupported`、`not_run`，Skip 不冒充
  阶段通过。

**可选消费者与独立实验**

- Headless/Qt Device Workbench 是 Runtime 的可选 commissioning/diagnostics 消费者；
- Actuator 01 目前只是 `MOCK / ISOLATED`，不发运动 CAN 帧；
- Modbus、EtherCAT 和多总线 observer 保持独立实验，不进入 V1 Runtime Core。

项目不声明硬实时、功能安全、认证急停、真实执行器闭环或 physical CAN 已完成。

## 目录

```text
linux/       C++20 Runtime、daemon、测试以及可选 Workbench
protocol/    冻结的 CAN V1 线级合同与 golden vectors
deploy/      Orange Pi release/systemd/bring-up 合同
experiments/ 独立实验；不由 linux/ 递归构建
firmware/    可选 MCU 实验边界；V1 不构建
evidence/    可复现证据与脱敏摘要
docs/        架构、原理、计划、Workbench 和作品集材料
```

`linux/src/` 按 `core`、`can`、`linux`、`runtime`、`supervision`、`daemon`、`sim`
表达职责归属；不把目录当成必须逐层抽象的依赖框架。Workbench 在自己的
`application/services/profile` 目录内组织，不反向拥有 Runtime 状态或 CAN fd。

文档从 [docs/README.md](docs/README.md) 进入。日常只需：

- [系统边界](SPEC.md)
- [Runtime 架构](docs/ARCHITECTURE.md)
- [学习与面试知识库](docs/KNOWLEDGE_BASE.md)
- [当前发布 Gate](docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md)
- [Workbench 入口](docs/workbench/README.md)
- [作品集入口](docs/portfolio/README.md)

## 构建与测试

默认构建 Runtime 和无 Qt Workbench；Qt UI 默认关闭。

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

需要 Qt6 UI 时显式打开：

```bash
cmake -S linux -B build/qt-on \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRCR_BUILD_TESTS=ON \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON
cmake --build build/qt-on -j2
ctest --test-dir build/qt-on --output-on-failure
```

## 最小运行路径

创建 `vcan0` 需要主机权限；库和 daemon 不会自行修改网络接口。

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
```

终端 A 启动节点模拟器，终端 B 启动 daemon：

```bash
./build/linux/rcr_node_sim --can vcan0 --node-id 1 --duration-ms 2000
./build/linux/rcrd --can vcan0 --node-id 1 --duration-ms 1000
```

阶段验收必须显式要求 vcan；缺接口不能用 Skip 凑 PASS：

```bash
./build/linux/tests/test_socketcan_vcan --require-vcan
./linux/scripts/run_vcan_acceptance.sh vcan0
./linux/scripts/run_fault_matrix.sh vcan0
```

周期 benchmark 测量空 callback 的唤醒 lateness，不是 CAN/control 端到端延迟：

```bash
./build/linux/rcr_benchmark \
  --duration-ms 10000 --period-us 1000 --samples-out /tmp/rcr-samples.txt
```

Workbench 的运行、线程、Mock 边界和证据见
[docs/workbench/README.md](docs/workbench/README.md)，不在顶层 README 复制维护。

## 下一步

不增加页面、协议或硬件分支。先按发布 Gate 完成：迁移收口 → 文档一致 → ThinkPad clean
证据 → Orange Pi 同 commit 证据 → 发布摘要。物理 CAN 的前置识别、回滚和设备树 Gate
只有 V1 发布后又经评审选为下一独立 Gate，才按
[物理 CAN 候选执行方案](docs/plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md) 重开。
