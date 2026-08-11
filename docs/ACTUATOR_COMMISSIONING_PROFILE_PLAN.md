# Actuator 01 Commissioning Profile 详细设计

状态：**Phase 5A isolated MOCK implemented locally；Runtime admission / clean evidence open**

上位产品：[`Device Test & Diagnostic Workbench`](DEVICE_TEST_DIAGNOSTIC_WORKBENCH_DEVELOPMENT_PLAN.md)

文档日期：2026-08-11

> 本文恢复并保留原 Robot Actuator Commissioning Console 的详细工程设计。定位纠偏后，
> 它不再代表整个 Qt 工具，而是 Workbench 的第一个机器人执行器设备 profile。

本文不是 CNC controller 计划，不声称真实 servo、CAN HAT、STM32、电机或安全功能已经验证。
Workbench Phase 1～4 已关闭。当前已实现 A0/A1/A3 的隔离 Mock 切片；A2 Runtime admission、
A4 CAN contract 和 A5 physical integration 仍未实现。

## 1. Profile 目标

它回答：

> `Actuator 01` 为什么没有正常使能、建立参考零位、跟随目标、停止或完成故障恢复？

长期链路：

```text
Qt Device Test & Diagnostic Workbench
        │ Actuator 01 profile
        ▼
Linux Runtime
 ├─ Runtime authority
 ├─ command freshness / watchdog
 ├─ device supervision
 ├─ fault containment
 └─ trace
        │ SocketCAN（未来实物合同）
        ▼
STM32 actuator node
 ├─ encoder
 ├─ local 1 kHz PI
 ├─ PWM
 └─ endpoint fault / output lease
        ▼
motor / servo-like actuator
```

Linux 与 Qt 都不运行 1 kHz 电流/速度闭环。Qt 只做低频操作、观察与测试，Linux Runtime
负责命令权限和监督，MCU 才拥有设备端实时输出。

## 2. 名称与单位

界面使用机器人语义：

- `Actuator 01` / `Joint 01` / `Motor Node 01`；
- `Drive Enable`，必要时括注 servo-like enable；
- `Actuator Jog -/+` 或 `Joint Jog -/+`；
- `Tracking Error`，文档中说明与工业语境的 following error 对应；
- position 默认使用 `rad`，velocity 根据 wire contract 决定 `rad/s` 或 `rpm`；
- 所有单位在协议、模型、UI 和测试 criteria 中必须一致，不做隐式换算。

不使用 `Axis X`、CNC 坐标系、刀具或加工语义。

## 3. 功能范围

### 3.1 Runtime 状态

显示：

```text
Runtime State
Backend / evidence label
Scheduler state
Watchdog state and age
Device Supervisor state
Runtime uptime
```

Mock 示例必须明确写：

```text
Runtime       ACTIVE
Backend       MOCK
Heartbeat     SIMULATED / 8 ms
Supervisor    HEALTHY
```

### 3.2 Actuator 状态与 telemetry

```text
Actuator State
Drive Enabled
Homed
Link / Heartbeat age

Target Position
Actual Position
Position Error

Target Velocity
Actual Velocity
Velocity Error

Soft Limit [min, max]
Active Fault / Reject Reason
```

操作：

```text
[Drive Enable] [Drive Disable]
[Start] [Normal Stop] [QUICK STOP]
[Actuator Jog -] [Actuator Jog +]
[Home]
[Reset Fault]
```

`QUICK STOP` 只是软件快速停止请求。没有硬件安全回路时禁止写 `EMERGENCY STOP`。

### 3.3 Fault Injection

独立区域明确标为 `TEST / MOCK`，候选注入：

- Communication Timeout；
- Encoder Fault；
- Tracking/Following Error；
- Positive Limit；
- Negative Limit；
- Device Fault。

Fault Injection 默认关闭，不与普通 command API 混用；实物协议不得为了 UI 演示加入未评审
的生产 fault command。

## 4. 分层与所有权

```text
MainWindow / profile widgets
        │ signal / slot
        ▼
WorkbenchController
        │ typed command / immutable snapshot
        ▼
MockActuatorProfile（当前，MOCK / ISOLATED）

future Runtime admission → future explicit actuator CAN session
```

`MainWindow` 只负责 presentation。禁止把 actuator state machine、motor simulation、CAN loop、
watchdog 或 fault transition 放进 UI class。

当前只有 Mock 实现和未来 CAN 目标，不能为了树形图创建 `IMotionDevice`、driver registry 或
通用 plugin framework。出现第二个真实且行为不同的实现后，再根据重复点评审窄接口。

## 5. Runtime 与 Actuator 状态机

### 5.1 两层状态不能混成一个 enum

Runtime 表达整条 Linux 控制路径是否允许命令：

```text
Disabled → Idle → Active
                  ├→ Hold
                  └→ Fault
```

Actuator profile 表达单个设备的局部运动阶段：

```text
DISABLED
   │ Drive Enable
   ▼
IDLE ── Home ──> HOMING ── complete ──> READY
 │                  │                       │
 │                  └──── fault ────────────┤
 │                                          │ command / jog
 └─ disable                                 ▼
                                        RUNNING
                                            │ stop
                                            ▼
                                        STOPPING
                                            │ velocity≈0
                                            ▼
                                          READY

any active state ── fault ──> FAULT
FAULT ── Reset Fault + blockers clear ──> DISABLED or IDLE
```

状态数量以表达非法转移为目的，不为了展示 state pattern 扩张。至少拒绝：

- `DISABLED → RUNNING`；
- `FAULT → RUNNING`；
- 未 Drive Enable 时 Home/Jog/Start；
- 已有 active motion 时直接启动另一不兼容 motion；
- fault blocker 未清除时 Reset Fault；
- Runtime 不在 Active 时的 motion-authorizing command。

### 5.2 命令分类

命令不是一律“Active 才能发”：

- motion-authorizing：Start、Jog press、Home、Drive Enable；
- motion-reducing：Normal Stop、Quick Stop、Jog release、Drive Disable；
- recovery：Reset Fault；
- observation：snapshot/telemetry，不改变状态。

Runtime fault 或 Hold 后必须继续允许能降低输出的本地路径；不能因为“非 Active 全拒绝”而
阻止 Stop。具体 authority 由 Runtime 评审，不由 Qt 自行决定。

## 6. Runtime command admission

当前 `OutputCommand` 是普通数字输出 bitmask，`CommandMailbox` 也是 latest-wins 输出邮箱。
它不能承载 Home、Jog release、fault edge 或 actuator telemetry。禁止发送假的数字输出命令
给 actuator watchdog 续租。

推荐最小集成合同：每个 Runtime 实例只配置一个 command domain。

```text
RuntimeCommandDomain
  ├─ OrdinaryOutput   # 当前 rcrd 默认路径
  └─ Actuator         # profile service

RuntimeCommandEnvelope
  ├─ session_id
  ├─ sequence
  └─ deadline_ns

RuntimeCommandAdmission
  ├─ domain
  ├─ active_generation
  ├─ session_id
  ├─ sequence
  └─ deadline_ns
```

约束：

- command payload preflight 通过后，才向 Runtime 请求 admission；
- admission 与 Runtime state/session/sequence/deadline 检查保持同一事务边界；
- 只有真正被 profile 状态机接受的 motion command 才能刷新 authority lease；
- domain 不匹配、旧 sequence、旧 session、过期 deadline 都拒绝；
- Runtime 离开 Active 时递增或撤销 `active_generation`，旧 admission 立即失效；
- OrdinaryOutput 与 Actuator 不得交替刷新同一 watchdog；
- 不创建 map、channel registry、generic message bus 或第二个 Runtime state machine。

如果该窄接点无法在不破坏 `LinuxRuntime::raise_fault()` 原子性的前提下加入，则 Actuator
profile 只能保持独立 Mock，标注“尚未接入 Runtime”。

## 7. 三层时效与失效

### 7.1 Runtime authority lease

证明 Linux Runtime 当前仍允许该 session 的 actuator 命令。Qt 低频 keepalive 或测试步骤只
能刷新自己的 authority；不能掩盖普通输出 domain 已失去 freshness。

### 7.2 Jog deadman

Jog 是按住生效的临时运动，至少需要：

```text
press → start jog with direction / velocity / deadline
periodic renew while held
release → stop jog
renew timeout → stop jog
Stop / Quick Stop / fault → stop jog and invalidate token
```

不能只依赖 Qt button release；窗口失焦、事件丢失或进程崩溃都可能让 release 永远不到达。
每次 press 产生新的 jog token/generation，旧 renew 或旧 release 不得影响新动作。

### 7.3 MCU endpoint output lease（未来）

即使 Linux 已停止发送，MCU 仍可能保留最后一次 Applied PWM/velocity target。因此真实节点
必须有独立 endpoint lease：在有限时间未收到合法新命令时，本地把输出降为中性并上报状态。

Runtime authority lease、Jog deadman 和 MCU endpoint lease 是三种不同失效范围，不能用一个
GUI timer 代替。

## 8. Homing

Mock 只建立正确概念，不伪造工业 homing algorithm：

```text
READY/IDLE → HOMING
HOMING + elapsed >= configured duration → READY, homed=true, position=0
HOMING + Stop → STOPPING → IDLE/READY（homed 保持策略需固定）
HOMING + fault/limit → FAULT, homed=false
```

真实实现前必须定义 reference sensor、方向、速度、超时、越程、重复精度和中断恢复。没有这些
实物条件时只能显示 `MOCK HOMING`。

## 9. Mock actuator 动态

Mock 使用确定性一阶响应，不使用随机 rpm：

```text
velocity += alpha * (target_velocity - velocity)
position += velocity * dt
```

建议：

- model tick：10 ms；
- UI refresh：50～100 ms；
- `alpha`、stop alpha、quick-stop alpha 固定在 config；
- 使用单调时间并限定异常大 `dt`，避免 debugger pause 后位置跳变；
- Normal Stop 将 target 置零并用普通减速度；
- Quick Stop 使用更高减速度，但不瞬间传送到零；
- 相同输入序列必须产生相同 telemetry，便于测试。

Mock tick 与 Qt event loop 解耦。第一阶段若模型轻量且只用于 UI demo，可由单个 `QTimer`
驱动；但 headless tests 必须能显式推进时间，不能依赖真实 sleep。

## 10. Limits 与 tracking error

### 10.1 Soft Limit

配置：

```text
min_position
max_position
```

规则：

- position target 在接纳前检查；越界命令拒绝，不改变旧 target；
- velocity/Jog 需要根据当前位置、方向和 braking policy 防止继续越界；
- 正向越界映射 `SOFT_LIMIT_POSITIVE`，负向越界映射 `SOFT_LIMIT_NEGATIVE`；
- 优先扩展/映射现有 fault 分类和诊断 context，不建立平行 Runtime fault manager；
- configuration error（min >= max）在启动时拒绝。

### 10.2 Tracking / Following Error

根据控制模式定义，不能混用：

```text
position_error = target_position - actual_position
velocity_error = target_velocity - actual_velocity
```

Mock 配置 threshold 与持续周期 `N`。只有连续 N 个有效样本超阈值才触发，任一有效样本恢复
则计数清零；invalid/stale sample 应进入 communication/device diagnosis，不能当作误差为零。

触发路径：

```text
RUNNING
  → revoke target / request stop
  → Runtime fault transaction
  → Actuator FAULT
```

先撤销继续授权，再进入可观察 fault；不能只在 UI 上点亮红灯。

## 11. Fault 与恢复

候选 profile reason：

```text
SOFT_LIMIT_POSITIVE
SOFT_LIMIT_NEGATIVE
FOLLOWING_ERROR
ENCODER_FAULT
COMMUNICATION_TIMEOUT
DEVICE_FAULT
```

这些 reason 是设备诊断上下文；是否映射 `FaultCode::CommLoss/NodeFault/Internal` 由现有 Runtime
fault architecture 决定。`FaultCode` 是分类，不等于活动 blocker 集合。

Reset Fault：

1. 拒绝所有 motion-authorizing command；
2. 确认 velocity/output 已为中性；
3. 检查 communication、limit、encoder 等 persistent blockers 已解除；
4. 清设备 fault/ack（若协议支持）；
5. 只有收到确认才允许 Runtime recovery；
6. 回到 `DISABLED` 或明确的非运动状态；
7. 不自动重放 fault 前的旧 target。

Mock injection 必须复用同一恢复路径，不能直接把 bool 改回 false。

## 12. Qt signal/slot 数据流

```text
UI press
  → MainWindow signal
  → WorkbenchController slot
  → validate presentation input
  → ActuatorProfileService request
  → Runtime/device result
  → immutable snapshot / command result signal
  → UI render

device/model tick
  → typed telemetry snapshot
  → bounded buffer / latest snapshot
  → Controller signal
  → UI QTimer decimated refresh
```

线程规则：

- QWidget 只在 UI thread 访问；
- queued connection 跨线程传递拥有自己生命周期的数据；
- 不把裸指针或可变 device object 发给 UI；
- CAN worker 阻塞在 fd/epoll，不在 UI thread 读 socket；
- UI refresh 慢时允许合并 telemetry snapshot，fault/transition edge 不得 latest-wins 丢失；
- shutdown：停止新命令 → Quick/neutral policy → 停 worker → join → 销毁 QObject/UI。

第一阶段没有阻塞 I/O 时无需为了展示 QThread 创建线程。真实 SocketCAN session 进入后，再用
worker-object pattern 评审专用线程。

## 13. UI 区域

Actuator profile 在 Workbench 页面中提供，而不是再创建一套独立主窗口：

1. Runtime/Connection：state、backend、watchdog、supervisor、uptime；
2. Actuator：state、enabled、homed、target/actual/error；
3. Manual Control：Enable、Jog、Home、Start、Stop、Quick Stop、Reset；
4. Communication：link、heartbeat age、last update、明确的 simulated/physical label；
5. Diagnostics/Trend：target velocity、actual velocity、position error、fault events。

趋势只保留最近 5～10 秒。没有必要时不引入 Qt Charts、大型绘图库或数据库。

## 14. CMake 边界

Actuator profile 必须建立在上位 Workbench 可选构建内：

```text
RCR_BUILD_QT_DEVICE_WORKBENCH=OFF
  → rcr / rcrd / tests 正常构建，无 Qt dependency

RCR_BUILD_QT_DEVICE_WORKBENCH=ON
  → additionally build Qt6 Workbench + Actuator profile UI
```

无 Qt 的 Mock model、状态机和 evaluator 应位于 `rcr_workbench` 或更窄的 headless target；
QObject、QTimer、QThread 和 Widgets 只进入 Qt target。Qt 不进入 `rcr`、fault、SocketCAN core。

## 15. 自动测试矩阵

实现后至少覆盖：

1. `DISABLED → Drive Enable → IDLE/READY`；
2. Start 后 actual velocity 确定性收敛到 target；
3. Normal Stop 后 velocity 收敛到零；
4. Jog + 产生正向运动；
5. Jog - 产生负向运动；
6. Jog release 停止续租；
7. 丢失 release 时 deadman timeout 停止；
8. Home 进入 HOMING，完成后 homed=true、position=0；
9. Homing fault 进入 FAULT；
10. 正/负 soft limit 拒绝或 fault，旧 target 不被污染；
11. tracking error 连续 N 周期触发 fault；
12. 单个误差 spike 不触发持续 fault；
13. communication timeout 触发现有 supervisor/fault response；
14. FAULT 中 Start/Jog/Home 被拒绝；
15. blockers 未解除时 Reset Fault 被拒绝；
16. Reset 成功回到非运动状态且不重放旧 target；
17. Normal Stop 与 Quick Stop 有可测量的不同减速行为；
18. Runtime 离开 Active 后旧 admission 失效；
19. Qt/controller source 消失后 authority/deadman timeout 生效；
20. 原仓全部测试无回归。

Mock 测试、vcan 测试和 physical bench 测试必须分组报告。

## 16. 分阶段 Gate

### A0 — Profile contract

冻结单位、状态、命令分类、fault mapping 和 evidence label；不写 UI。

当前：`implemented locally`。单位为 rad/rad/s，证据固定为 `MOCK`，类型位于
`mock_actuator_profile.hpp`。

### A1 — Deterministic headless Mock

实现状态机、模型、Homing、Jog deadman、limits、tracking error 和单元测试；尚未接 Runtime 时
明确标注 isolated Mock。

当前：`implemented locally`。13 个确定性场景通过；模型不创建线程、不读 CAN、不使用随机数。

### A2 — Runtime admission

以最小改动接入 command domain、session/sequence/deadline 和 active generation；现有
OrdinaryOutput 行为保持不变。

当前：`not implemented`。审查确认现有数字输出 mailbox 不能承载运动命令，因此未伪装接入。

### A3 — Workbench profile UI

Qt 只消费已验证的 headless snapshot/command contract；验证 thread affinity 与关闭顺序。

当前：`implemented locally`。Actuator 01 页和 offscreen
`--run-actuator-smoke-once` 已通过；尚无人工视觉验收或 clean-commit evidence。

### A4 — CAN simulator contract

冻结显式 actuator wire contract、codec 和 golden vectors，再接 simulator；不能复用数字输出帧
冒充速度控制。

### A5 — Physical integration

确认 HAT SKU、电压、晶振、CS/INT、Orange Pi BSP/DTO、终端电阻、MCU 固件版本和 rollback
后才上电。记录 physical evidence，不把 Raspberry Pi overlay 搬到 Orange Pi。

## 17. 明确延期

- G-code、CNC program、tool compensation、cutting process；
- XY machining semantics、CNC coordinate systems、arc interpolation；
- 多关节 trajectory、ROS 2、MoveIt；
- two-axis coordinated motion / linear interpolation；
- 电流环、FOC、1 kHz PI、encoder/PWM 固件实现；
- 认证 E-stop/STO/SS1/SLS；
- 未出现第二个真实 backend 前的 generic device framework。

## 18. 面试证据边界

设计完成后可以解释：

- Qt event loop、QTimer、signal/slot、thread affinity 和 worker object；
- C++ 对象生命周期、RAII、Result、atomic/mutex 与 immutable snapshot；
- Runtime 权限状态与局部 actuator 状态为什么分层；
- Enable、Jog deadman、Homing、Soft Limit、Tracking Error、Quick Stop、Fault Reset；
- Linux authority lease、设备 endpoint lease 和 MCU local loop 的不同时间尺度；
- Mock 为什么确定性、测试怎样验证 cleanup 和 fault path。

只有对应阶段实际通过后，才能说“在代码中使用过”或“在硬件上测量过”。当前本文只是详细
设计，不能说已经做过真实 servo/CNC、物理 CAN、hard real-time 或功能安全系统。
