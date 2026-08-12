# 系统架构

**Authority**：本文是系统组件关系、运行上下文和依赖方向的主 Architecture 文档；范围与
能力边界以 [SPEC.md](../SPEC.md) 为准，代码归属以
[CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md) 为准。实现细节接着读
[LINUX_RUNTIME.md](LINUX_RUNTIME.md)，daemon 生命周期合同读
[RCRD_CONTRACT.md](RCRD_CONTRACT.md)。本文不负责当前 Gate，也不拆 Workbench 目录。

## 1. 上下文

```text
ThinkPad                                   Orange Pi 4 Pro 4GB
┌──────────────────────┐   SSH / source   ┌────────────────────────┐
│ edit / test / review │ ───────────────► │ systemd / Runtime      │
│ baseline benchmark   │ ◄─────────────── │ benchmark / evidence   │
└──────────────────────┘   logs/results   └───────────┬────────────┘
                                                     │ SocketCAN
                                                     ▼
                                                   vcan0
                                                     │
                                                     ▼
                                             CAN Node Simulator
```

ThinkPad 回答“代码是否正确、x86 基线如何”；Orange Pi 回答“ARM Linux 上能否部署、
调度权限是否正确、压力下延迟如何”。二者不能互相替代。

ESP32-S3 与 STM32F103 不在 V1 运行图中。ESP32 可作为后续 USB 节点实验；F103 仅在
出现明确裸机或物理 CAN 学习问题时启用。

## 2. 软件职责分区

仓库沿用“五层一横”这个历史名称，但五区表达稳定职责，不是严格单向、只能调用相邻区的
OSI 式层级；Evidence Plane 横跨所有区域。责任与旧 A–G 证据路线见
[“五层一横”架构与 A–G 证据路线](FIVE_LAYERS_ONE_PLANE.md)。

```text
Protocol Contract       CAN V1 wire format / codec / golden vectors
Runtime Semantics       StateMachine / Watchdog / Mailbox / Queue / Trace / LinuxRuntime
Linux Mechanisms        Scheduler / fd / epoll / SocketCAN / pthread
Process Orchestration   RuntimeDaemon / Device Supervision / startup / shutdown
Deployment              ThinkPad → Orange Pi → systemd

Evidence Plane          test / fault / benchmark / trace / metadata / knowledge cards
```

`RuntimeDaemon` 会直接组合 Runtime、Linux fd/线程和 CAN 协议路径，因此这些区域不要求
相邻单向调用。边界规则约束“谁不能决定什么”：

- 协议层定义线上字节和非法输入；不创建线程、不打开 socket、不访问状态机。
- Runtime Semantics 管状态、命令新鲜度、背压和输出事务；不负责进程退出码和 systemd。
- Linux 机制层管理线程属性、fd 生命周期和非阻塞收发；不决定状态恢复策略。
- Device Supervision 解释 heartbeat/session/CommLoss 并决定故障升级，不归入纯 Core。
- Process Orchestration 组合资源与关闭顺序，不重新实现协议、epoll 或状态机。
- 部署层管理服务用户、systemd、日志和平台证据，不侵入 Runtime Core。
- 节点模拟器是独立进程，只通过 SocketCAN 观察系统，不能读取 Runtime 内存。
- 只有物理 CAN 真正出现后，`vcan0` 才切换为 `can0`；不先建通用 Transport。

## 3. 线程与事件流

```text
main/Application ─ publish_output_command ─┐
                                          ├─ state mutex ─ mailbox
periodic thread ─ watchdog check ──────────┘

I/O thread ─ epoll(SocketCAN, eventfd, signalfd)
    ├─ CAN frame → decode → bounded event path → Runtime
    └─ stop/signal → lifecycle
```

周期线程只执行有界监督逻辑。socket 等待属于 I/O 线程；日志落盘属于非周期上下文。
`EpollReactor`、`SocketCan`、I/O 线程和有界输入队列已经在 `rcrd` 集成；systemd unit
静态资产已经落地。具体 Orange Pi 实机状态只由
[当前发布 Gate](plans/PORTFOLIO_V1_RELEASE_PLAN.md) 和对应 evidence 说明维护。

## 4. 状态与命令关系

普通输出命令只有在以下条件全部满足时才进入 mailbox：

```text
scheduler_running
AND mode == Active
AND interlock_ready
AND session_id != 0
AND sequence strictly increases
AND deadline_ns > CLOCK_MONOTONIC now
AND mask != 0
```

离开 Active 会原子化地关闭 watchdog、清空 mailbox 并遗忘活动会话。Hold 恢复只到
Idle，必须显式再次 Activate，从而阻止旧输出自动恢复。

## 5. 后续适配边界

- Device Workbench：可选应用/展示平面，**不是**五层一横的第六层。源码按
  `ui / controller / services / application / profile` 分层；文档入口见
  [docs/workbench/README.md](workbench/README.md)。Qt 只消费低频 snapshot 与显式
  use-case reply，不拥有 `DaemonSnapshot`、`CanFrame` 或 SocketCAN。CAN Health 经
  Adapter 读 Runtime 已解码快照，不另开 CAN socket。当前仍为同进程接缝，进程级
  crash containment / IPC / A2 Runtime admission 尚未实现。Direct CAN bench 仍是
  延期候选。
- ROS 2 Adapter：单独组件，只做 Topic/Runtime API 转换。
- Dashboard：只读消费状态和 trace，不成为高频命令源。
- ESP32 USB：独立实验，不迫使 CAN 核心提前抽象为 Transport 框架。
- PREEMPT_RT：同硬件、同负载和同 benchmark 的独立对照实验。
- EtherCAT：具身系统平台方向在 Orange Pi 部署后优先，首轮主站实验使用 ThinkPad 的
  板载 Intel 有线网口；以独立 SOEM + I/O SubDevice 验证，通过 Gate 后才设计 Runtime
  Adapter。Orange Pi 4 Pro 的板载千兆网口只作为后续 ARM 对照候选，不提前改变 Core，
  也不把 x86 与 ARM 网卡结果混成一个结论。
- Modbus：EtherCAT 基线后作为独立 TCP/RTU 外围设备实验，详见
  [开发路线](plans/DEVELOPMENT_ROADMAP.md)与[通信演进边界](COMMUNICATION_EVOLUTION.md)；真实设备
  需求出现前不接入 Runtime，也不与 CAN/EtherCAT 合并成通用 Transport。
- 多源观测 → 执行接点：观测实验与 Runtime 命令路径的边界合同见
  [观测→执行接点合同](OBSERVATION_TO_EXECUTION_CONTRACT.md)（仅冻结职责，**未实现**链路；
  禁止在周期 callback 内做融合/慢 I/O）。
