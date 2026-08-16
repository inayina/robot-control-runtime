# 系统架构

**权威**：系统组件关系、运行上下文和依赖方向。范围以 [SPEC.md](../SPEC.md)
为准，代码归属以 [CODE_OWNERSHIP_MAP.md](CODE_OWNERSHIP_MAP.md) 为准。daemon 生命周期
读 [RCRD_CONTRACT.md](RCRD_CONTRACT.md)。CAN 线级合同读 `protocol/can_v1/`。

**Portfolio V1 已冻结。** 本文是唯一主系统图。Remote LOOPBACK、Actuator MOCK、Mock
Modbus、EtherCAT / ROS 2 / PREEMPT_RT 候选是次要 / 历史 / 实验材料，不进入主图。

## 系统上下文

```text
                        ThinkPad
               ┌─────────────────────┐
               │ Qt 工程站            │
               │ 观察 / 下发          │
               └──────────┬──────────┘
                          │
                       CEL1/TCP
                          │
──────────────────────────┼────────────────────
                          │       Orange Pi
                          ▼
                ┌─────────────────────┐
                │    rcr_cell_app     │
                │ 边缘应用             │
                │                     │
                │  ┌───────────────┐  │
                │  │ RuntimeDaemon │  │
                │  │ 状态          │  │
                │  │ watchdog      │  │
                │  │ 监督          │  │
                │  │ 命令新鲜度     │  │
                │  │ 调度器        │  │
                │  └──────┬────────┘  │
                │         │           │
                │ CellReadyMapper     │
                └────┬───────────┬────┘
                     │           │
                 SocketCAN   localhost TCP
                     │           │
                     │           ▼
                     │   ┌────────────────────┐
                     │   │ rcr_modbus_rtu_    │
                     │   │ agent              │
                     │   │ 拥有 /dev/ttyS7    │
                     │   └─────────┬──────────┘
                     │             │
                     │          Modbus RTU
                     │             │
                     ▼             ▼
                STM32F103       MR0-IOR08
                 │     │           │
               SG90   PA0         DO0
                      红外
```

ThinkPad 回答“代码是否正确”并作为工程站。Orange Pi 承担闭环 Runtime。关掉 Qt 后
CellReady→DO0 仍在板上。

`RuntimeDaemon` 只有一个。两个宿主：

```text
             RuntimeDaemon
              /         \
           rcrd       rcr_cell_app
        独立宿主         边缘演示 / 应用
```

作品集主演示是 `rcr_cell_app`。不要 `rcrd` 与 `rcr_cell_app` 同时写 `can0`。

## 所有权

每个答案只能有一个。

| 对象 | 拥有者 | 不拥有 |
|---|---|---|
| Runtime 状态 / watchdog / 监督 / 命令准入 / 调度器 / SocketCAN 生命周期 | `RuntimeDaemon`（演示宿主：`rcr_cell_app`） | Qt、STM32、Modbus agent |
| CAN 节点 / SG90 PWM / PA0 / `POSITION_REACHED` | STM32F103 | CellReady、Modbus、Qt、Linux Runtime 状态 |
| `机器人状态 → CellReady` | 边缘 `CellReadyMapper` | CAN decoder、MCU、Runtime Core、Qt |
| TCP→RTU→`/dev/ttyS7`→MR0 | `rcr_modbus_rtu_agent` | “为什么 DO0 该 ON” |
| MR0 DO0 线圈 | 物理模块；软件只报告 requested/confirmed | CellReady 策略 |
| Overview / 工程命令 | Qt `--cell-peer` | CAN fd、watchdog、CellReady、自动 DO0、安全功能 |

引脚冻结：`PA8` SG90 PWM，`PA0` TARGET_SENSOR_DO，`PA11` CAN RX，`PA12` CAN TX。

`CellReady`（与代码一致）：

```text
device.online
AND Runtime Active
AND POSITION_REACHED
AND device_fault_code == 0
AND Runtime fault == None
```

CAN 丢失与 RS-485 丢失语义不同：前者走既有 CommLoss / Hold-Fault，旧命令不再被正常执行；
后者 Cell I/O OFFLINE/TIMEOUT，Qt 仍可用，恢复后需显式 Probe，不静默重放 DO0。

## 主数据流

### 机器人命令

```text
Qt
→ CEL1
→ rcr_cell_app
→ Runtime 命令准入
→ SocketCAN
→ STM32
→ SG90 (PA8)
```

HOME / TARGET 是经准入下发的物理工程输出位，不是 Runtime 状态迁移。

### 机器人反馈

```text
PA0 红外
→ STM32 去抖
→ POSITION_REACHED
→ CAN NodeStatus.input_bits bit0
→ NodeSupervisor
→ Runtime / 应用快照
```

### 单元输出

```text
POSITION_REACHED
+ Runtime Active
+ 设备健康
→ CellReadyMapper
→ 本机 Modbus agent
→ MR0 DO0  (requested ≠ confirmed)
```

`--cell-peer` 下 Qt 只读 CEL1 上由边缘拥有的 DO0，不再当第二套自动 Modbus 拥有者。

## 软件职责分区

仓库沿用“五层一横”这个历史名称，但五区表达稳定职责，不是严格单向 OSI 层；证据平面
横跨所有区域。细节见 [FIVE_LAYERS_ONE_PLANE.md](FIVE_LAYERS_ONE_PLANE.md)。

```text
协议合同     CAN V1 线格式 / codec / golden vectors
Runtime 语义  StateMachine / Watchdog / Mailbox / Queue / Trace / LinuxRuntime
Linux 机制    Scheduler / fd / epoll / SocketCAN / pthread
进程编排     RuntimeDaemon / 设备监督 / 启动 / 关闭
部署         ThinkPad → Orange Pi → systemd

证据平面     测试 / 故障 / benchmark / trace / 元数据 / 知识卡
```

边界规则：

- 协议层定义线上字节；不创建线程、不打开 socket、不访问状态机。
- Runtime 语义管状态、命令新鲜度、背压和输出事务；不负责进程退出码和 systemd。
- Linux 机制层管理线程属性、fd 生命周期和非阻塞收发；不决定状态恢复策略。
- 设备监督解释 heartbeat/session/CommLoss 并决定故障升级。
- 进程编排组合资源与关闭顺序，不重新实现协议、epoll 或状态机。
- 节点模拟器是独立进程，只通过 SocketCAN 观察系统。
- Qt 是可选工程站，不是第六层，也不是安全权威。

实现细节接着读 [LINUX_RUNTIME.md](LINUX_RUNTIME.md)。

## 线程与事件流

```text
main/Application ─ publish_output_command ─┐
                                          ├─ state mutex ─ mailbox
periodic thread ─ watchdog check ──────────┘

I/O thread ─ epoll(SocketCAN, eventfd, signalfd)
    ├─ CAN frame → decode → bounded event path → Runtime
    └─ stop/signal → lifecycle
```

周期线程只执行有界监督逻辑。socket 等待属于 I/O 线程。`EpollReactor`、`SocketCan`、
I/O 线程和有界输入队列已经在 `RuntimeDaemon` 集成。

## 状态与命令关系

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

离开 Active 会原子化地关闭 watchdog、清空 mailbox 并遗忘活动会话。Hold 恢复只到 Idle，
必须显式再次 Activate，从而阻止旧输出自动恢复。

## 次要 / 历史 / 实验（不进入主演示）

下列能力保留实现与测试，默认不出现在 README 主线或 Qt 顶层导航：

| 项 | 角色 |
|---|---|
| `rcrd` + `vcan0` | ThinkPad 软件对照与 systemd 宿主 |
| Lab / LOOPBACK | 远程控制面回环；`NO PHYSICAL PC-ARM` |
| Lab / Actuator MOCK | 隔离执行器状态机；不发运动 CAN |
| Mock Modbus | 无 RS-485 的工程回归 |
| `experiments/` EtherCAT、额外 Modbus、realtime | 独立实验，未接入 Runtime Core |

**不启动（已冻结）：** EtherCAT、ROS 2 Adapter、PREEMPT_RT、Web Dashboard、通用
Transport / Fieldbus manager、多节点 CAN、更多 Modbus 从站。
