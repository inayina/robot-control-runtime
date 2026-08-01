# 系统架构

权威范围见 [SPEC.md](../SPEC.md)。本文件只解释组件关系和职责边界。

## 1. 上下文

```text
ThinkPad                                   Orange Pi Zero 3W
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

## 2. 软件分层

```text
CLI / future ROS 2 Adapter
            │ typed API
            ▼
Application Layer
  session / validation / lifecycle
            │
            ▼
Runtime Core
  Scheduler / StateMachine / Mailbox / Watchdog / Trace
            │ frames + events
            ▼
Linux I/O Boundary
  EpollReactor / SocketCan / eventfd / signalfd
            │
            ▼
vcan0（V1） / can0（可选物理 CAN）
```

边界规则：

- Application 创建会话并表达操作意图，不操作 Linux fd。
- Runtime Core 管时间、状态、命令新鲜度和监督，不解析 ROS 2 Topic。
- Linux I/O Boundary 管 fd 生命周期和非阻塞收发，不决定状态恢复策略。
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
当前 `EpollReactor` 和 `SocketCan` 已各自实现并测试，但 I/O 线程和事件队列尚未集成。

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

- ROS 2 Adapter：单独组件，只做 Topic/Runtime API 转换。
- Dashboard：只读消费状态和 trace，不成为高频命令源。
- ESP32 USB：独立实验，不迫使 CAN 核心提前抽象为 Transport 框架。
- PREEMPT_RT：同硬件、同负载和同 benchmark 的独立对照实验。
- EtherCAT：具身系统平台方向在 Orange Pi 部署后优先，但主站实验使用 ThinkPad 的
  板载 Intel 有线网口；以独立 SOEM + I/O SubDevice 验证，通过 Gate 后才设计 Runtime
  Adapter，不把主站模型提前带入 Core，也不要求无板载网口的 Orange Pi 承担主站。
- Modbus：EtherCAT 基线后作为独立 TCP/RTU 外围设备实验，详见
  [开发路线](DEVELOPMENT_ROADMAP.md)；真实设备需求出现前不接入 Runtime。
