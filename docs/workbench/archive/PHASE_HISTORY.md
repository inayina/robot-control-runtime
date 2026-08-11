# Workbench 阶段流水账（归档）

状态：**Archived** — 不是当前合同。当前怎么跑见 [../README.md](../README.md)，
还开着的门见 [../GATES.md](../GATES.md)。下文只保留 Phase 0–6 当时的记录。

文档日期：2026-08-11

实现范围：**Headless Test Contract + 进程内 Runtime Application Adapter +
Runtime-connected CAN Health + 冻结结果 schema / 原子 JSON+CSV + 可选 Qt6
Widgets + 隔离 Actuator Mock**。IPC、Direct CAN、A2 Runtime admission 和实物执行器
尚未实现。

> 本工具就在 `robot-control-runtime` 仓库内开发，不新建仓库，也不把 Runtime
> 职责搬到其他项目。它是面向机器人底层设备的本地测试、bring-up 与诊断工作台；
> `Actuator 01` 是第一个设备场景，不是整个产品的唯一定位。

本文不改变 V1、Orange Pi、physical CAN、EtherCAT 或 Real-time Linux 的既有优先级。
进入任何实现阶段仍需显式确认。文中 `Mock`、`vcan` 和未来实物证据必须分开记录。

## 1. 定位纠偏

正式名称：

> **Device Test & Diagnostic Workbench**
>
> 机器人底层设备测试与诊断工作台

长期目标是帮助工程师回答：

> 某个机器人底层设备是否建立了通信、是否按合同响应、为什么测试失败、
> 失败证据在哪里？

目标链路仍然属于本仓：

```text
Qt Device Workbench
        │ manual command / test request / telemetry
        ▼
robot-control-runtime
 ├─ Runtime state and command authority
 ├─ watchdog and device supervision
 ├─ fault containment
 ├─ trace and evidence
 └─ SocketCAN
        │
        ▼
Real or simulated DUT
 ├─ current: vcan node simulator
 └─ future: MCU / actuator / sensor / I/O device
```

首个可见设备 profile 可以叫 `Actuator 01` 或 `Motor Node 01`，提供 Enable、Jog、
Homing、Soft Limit、Tracking Error 等通用运动执行概念。但工作台本身不能因此演化成
CNC controller，也不能声称尚未存在的伺服、MCU 或物理 CAN 已验证。

Actuator profile 的状态机、命令时效、Mock 动态、fault path、Qt 数据流和测试 Gate 不在本
总计划中重复压缩，完整保留在：

- [Actuator 01 Commissioning Profile 详细设计](../ACTUATOR.md)

## 2. 仓库边界

### 2.1 本仓继续拥有

- Linux/C++ Edge Runtime；
- Runtime state machine 和 command freshness；
- watchdog、device supervision、fault containment；
- scheduler、trace、logging 和 systemd；
- SocketCAN、CAN wire contract 和节点模拟器；
- 本地 commissioning、device test 和 diagnostic 工具；
- 与上述能力对应的自动化测试和可复现证据。

### 2.2 Qt Workbench 负责

- 连接配置和 backend/DUT 身份展示；
- 手动 bring-up 与受约束的设备命令；
- 原始通信观察、telemetry 和短时趋势；
- 自动测试用例的选择、执行、停止和进度展示；
- 通信、设备和测试诊断；
- CSV/JSON 测试结果浏览与导出；
- 清楚显示 `MOCK`、`VCAN`、`PHYSICAL` 等证据级别。

### 2.3 Qt Workbench 不拥有

- Runtime 全局状态、watchdog 或安全联锁的最终权威；
- 设备端 1 kHz 闭环、PWM、encoder ISR 或 MCU RTOS；
- Robot Ops Dashboard 的整机/任务运维视图；
- ROS 2 trajectory、MoveIt、Nav2、HOC 风险控制或 Task GT；
- G-code、加工程序、刀具补偿、CNC 坐标系或插补；
- FPGA/芯片实验室仪器、逻辑分析仪或示波器的虚构能力；
- 功能安全急停、STO、SS1/SLS 等认证能力。

## 3. Phase 0 仓库审计

### 3.1 当前结构

`linux/` 是独立 CMake 工程，沿用 C++20。现有主路径为：

```text
CLI / daemon / tests
        ↓
LinuxRuntime + RuntimeDaemon
        ↓
RuntimeStateMachine / PeriodicScheduler / Watchdog / Trace
        ↓
CanIoLoop / NodeSupervisor / SocketCan / EpollReactor
        ↓
vcan0 / CAN Node Simulator
```

Phase 0 审计时仓库没有 Qt、QML、Qt model/view、Serial、chart、Test Runner 或结果浏览模块。
随后已在本仓加入无 Qt 的 Headless Test Runner；本句保留的是审计时点，不是当前状态。

### 3.2 可以直接复用

- `LinuxRuntime`、`RuntimeStateMachine`：Runtime 权限和状态语义；
- `PeriodicScheduler`：单调时钟、绝对时间周期调度和调度降级统计；
- `MonotonicWatchdog`：freshness 检查原语；
- `LinuxRuntime::raise_fault()`：状态、fault、输出清理和 trace 的一致事务边界；
- `RuntimeDaemon`、`NodeSupervisor`：当前进程组合和单节点监督；
- `SocketCan`、`ICanBus`、`FakeCanBus`：CAN 专用收发与测试接缝；
- `EpollReactor`、`CanIoLoop`：Linux fd 数据路径；
- CAN V1 codec、golden vectors 和 `rcr_node_sim`；
- `TraceBuffer`、fault matrix 和 evidence 记录习惯；
- 现有测试与 `vcan` 端到端路径。

### 3.3 不能假设已经存在

- Qt 与 Runtime 间的 IPC；
- 通用 `IDevice`、`ITransport` 或 plugin framework；
- Serial transport；
- actuator position/velocity CAN 线级合同；
- MCU heartbeat、encoder、motor 或 servo telemetry；
- 物理 CAN/CAN HAT 实测；
- 可复用的自动 Test Runner 和结果 schema（Phase 3 已冻结
  `rcr.workbench.result.v1`；此处保留的是审计时点）。

### 3.4 当前验证基线

本次审计期间在临时构建目录运行现有 CTest：17 个通过、1 个跳过、0 个失败；跳过项为
需要 `vcan` 环境的 SocketCAN 测试。该结果只证明当前工作树上的普通软件回归情况，
不证明物理 CAN、真实执行器、硬实时或功能安全。

Foundation T0 加入 `test_workbench_runner` 后，全量 CTest 为 18 个通过、1 个 `vcan` 测试跳过、
0 个失败。新增 PASS 只覆盖 Test Runner 用户态生命周期。

本次 Runtime/Qt Boundary 加入 `test_workbench_runtime_adapter` 后，全量 CTest 为 19 个通过、
1 个 `vcan` 测试跳过、0 个失败。新增 PASS 只覆盖 headless DTO/Adapter；没有运行 Qt、physical
CAN 或真实设备。

本次 Phase 3 加入 `test_workbench_result_writer` 后，受限沙箱中的全量 CTest 为 21 个通过、
2 个跳过、0 个与本变更相关的失败；两个跳过项仍是无权打开 `PF_CAN` 的 vcan 目标。主机权限下
同一次构建为 23 个通过、0 个跳过、0 个失败，其中包括 CAN Health 的 vcan PASS 以及
`--fault-stop-heartbeat` / `--fault-send-illegal-after-ms` 注入 FAIL 证据。这些结果仍不是
physical CAN、真实设备或 Qt 证据。

## 4. 最小架构

### 4.1 同仓目录方案

Workbench 文件按本节 4.2 的新架构分层，不按历史「五层一横」去拆
`src/core|linux|runtime|daemon`。Runtime 那套源码分区仍然只服务 Runtime，不作为
Workbench 的目录坐标。

```text
robot-control-runtime/
├─ linux/
│  ├─ include/rcr/workbench/
│  │  ├─ application/   # DTO + RuntimeApplicationAdapter
│  │  ├─ services/      # TestRunner / ResultWriter / CAN Health
│  │  └─ profile/       # 隔离 commissioning Mock，不进 Runtime/CAN
│  ├─ src/workbench/
│  │  ├─ application/
│  │  ├─ services/
│  │  └─ profile/
│  ├─ tests/workbench/  # 无 Qt 的 runner / adapter / health / writer / mock
│  └─ tools/qt_device_workbench/   # OPTIONAL；默认不编
│     ├─ app/           # composition root
│     ├─ controller/    # 用例、QThread、metatype
│     └─ ui/            # Widgets，不判定
├─ protocol/            # 仍只放冻结的线级合同
└─ docs/workbench/      # 本文档层：计划 / Qt 合同 / 零基础笔记 / Actuator profile
```

不创建第九个仓库，不建立 root super-build，也不让 `firmware/` 被 Linux CMake 递归构建。
include 路径跟着层走（例如 `rcr/workbench/services/test_runner.hpp`）；不保留旧路径
redirect stub。namespace 仍是 `rcr::workbench`。

### 4.2 逻辑分层

```text
Qt Widgets UI
  Overview / Devices / Manual / Monitor / Tests / Diagnostics / Results
        │ signal / slot, presentation DTO
        ▼
Workbench Controller
        │ use-case request / immutable snapshot
        ▼
Headless Test & Diagnostic Services
  TestRunner / Evaluator / ResultWriter / DiagnosticRecorder
        │
        ├─ result persistence（本地文件，不进入 Runtime）
        └─ RuntimeApplicationAdapter（当前进程内；未来 IPC client）
                │ snapshot / explicit use-case command
                ▼
        RuntimeDaemon（唯一 Runtime / CAN fd owner）
                │
                ▼
        SocketCAN / CAN codec → DUT or rcr_node_sim
```

目录与层的对应：

| 层 | 头文件 | 实现 |
|---|---|---|
| Qt Widgets UI | `tools/qt_device_workbench/ui/` | 同左 |
| composition root | `tools/qt_device_workbench/app/` | 同左 |
| Workbench Controller | `tools/qt_device_workbench/controller/` | 同左 |
| Headless Test & Diagnostic Services | `include/rcr/workbench/services/` | `src/workbench/services/` |
| RuntimeApplicationAdapter + DTO | `include/rcr/workbench/application/` | `src/workbench/application/` |
| Isolated Mock actuator profile | `include/rcr/workbench/profile/` | `src/workbench/profile/` |
| RuntimeDaemon / SocketCAN | 既有 Runtime 目录，不因 Workbench 再拆 | 同左 |

Qt 只消费应用层接口，不把状态机、CAN receive loop、测试判定或结果写入塞进
`MainWindow`。

### 4.3 Phase 1 已落地的最小 Runtime 接缝

```text
future Qt Model / headless caller
        │ explicit use-case command / low-rate snapshot pull
        ▼
RuntimeApplicationAdapter
        │ maps application DTO; owns no thread or fd
        ▼
RuntimeDaemon
        ├─ LinuxRuntime / state / watchdog / scheduler
        ├─ NodeSupervisor
        └─ CanIoLoop / SocketCAN
```

公开 `application_model.hpp` 只依赖 C++ 标准库，定义 command、telemetry、feedback 和
diagnostic projection；它不暴露 `RuntimeMode`、`DaemonSnapshot`、`CanFrame`、QObject 或
Qt container。Adapter 是 `RuntimeDaemon` 的 non-owning 引用，不提供 daemon start/stop，
因此 Qt 不能借它接管 transport lifecycle。

Runtime → Qt 的目标数据流为：

```text
Runtime/Node/I/O snapshots
  → RuntimeApplicationAdapter::snapshot()
  → RuntimeTelemetrySnapshot
  → future Qt model
  → UI refresh (10–20 Hz, presentation only)
```

当前 snapshot 里的 `DiagnosticEvent` 仍是“生成这次 snapshot 时的诊断投影”，timestamp
是观察时刻，不是原始故障发生时刻。Phase 3 把一次 run 期间收集到的诊断复制进
`TestResult`，并与 criteria/measurement/reason/cleanup 一起原子写入 JSON/CSV；这不是
长期诊断数据库，也不替代 Runtime fault authority。

Qt → Runtime 的命令流为：

```text
future Qt action
  → explicit adapter method
  → RuntimeDaemon admission
  → LinuxRuntime state/mailbox
  → CanIoLoop / SocketCAN
```

Adapter 只做 UI 输入范围到当前数字输出合同的转换；是否接受、fault 状态和 session/sequence/
deadline 的最终判定仍由 Runtime 完成。当前是同进程接缝，尚不能证明 `Qt crash != rcrd crash`；
未来 IPC client 可替换 Adapter 的内部调用而不改变 presentation DTO。

### 4.4 为什么暂不创建通用设备/传输抽象

当前只有行为明确的 CAN 路径，没有第二个真实且行为不同的 backend。此时创建
`ITransport`、`IDevice`、driver registry 或 plugin system 只能预测未来需求，并容易把
CAN timeout、frame filtering 和 ownership 语义抹平。

本阶段保留 CAN 专用边界。Serial 只有在出现真实 DUT、帧合同和测试目标后再设计；
到那时再用两个实际实现评审是否存在可抽取的共同接口。

## 5. 三种运行模式与所有权

### 5.1 Runtime-connected mode（当前权威路径；IPC 是目标形态）

```text
当前：headless caller / future Qt
        → RuntimeApplicationAdapter（同进程）
        → RuntimeDaemon → Runtime authority → SocketCAN → DUT

目标：Qt → IPC client → rcrd → Runtime authority → SocketCAN → DUT
```

当前代码已经保持 application/Runtime/transport 的单向依赖，但仍是同进程接缝，不能声称
`Qt crash != Runtime crash`。进程隔离的目标是 Qt 退出后 `rcrd` 的 watchdog、fault handling
和设备降级继续运行；当前仓库还没有该 IPC，不为界面开发大规模重构 daemon。

### 5.2 Direct CAN bench mode（延期候选，不是当前主架构）

```text
Qt / headless CLI
  → Workbench application service
  → CAN-specific bench session
  → SocketCAN
  → DUT or rcr_node_sim
```

该模式未来可用于脱离 Runtime 的底层 bring-up 和协议测试，但当前没有实现。仅当以下 Gate
全部定义并验证后才能进入：CAN interface + DUT 的跨进程独占 lease、`rcrd` 冲突的 fail-closed
检测、命令 authority、崩溃/timeout 后的 lease 回收，以及先停止测试和命令再释放 socket 的
cleanup。只提示冲突或“能够再打开一个 CAN socket”都不算获得独占权。

在这些条件关闭前，Workbench 不建立第二个可写 CAN owner；当前 CAN Health 一律通过
`RuntimeApplicationAdapter` 只读采样 Runtime snapshot。

### 5.3 Simulation mode

使用 `FakeCanBus`、`rcr_node_sim` 或明确的 device model。所有 UI、结果和日志必须显示
`MOCK` 或 `VCAN`，不得把 Linux kernel `vcan` 证据写成物理总线证据。

## 6. UI 信息架构

UI 使用工程调试风格，优先信息密度、状态可读性和可复现步骤，不追求展示性动画。

| 页面 | 最小内容 | 明确不做 |
|---|---|---|
| Overview | backend、DUT、link、Runtime/test 状态、最近诊断 | 整机任务 Dashboard |
| Devices | node identity、heartbeat age、last update、profile | 通用设备插件市场 |
| Manual Control | profile 提供的受约束命令、Stop、Reset | 在 UI 内实现状态机 |
| Live Monitor | typed telemetry、age、短时趋势 | 数据库和长期历史 |
| CAN Monitor | id/direction/dlc/data/time、过滤、暂停显示 | 假造真实 bus rate |
| Tests | case、参数、precondition、run/stop/progress | DSL 和流程编排平台 |
| Diagnostics | communication/device/test 事件与上下文 | 替代 Runtime fault authority |
| Results | PASS/FAIL、criteria、measurement、CSV/JSON | PDF/HTML 报告系统 |

首个 Actuator profile 可显示 Enable、Jog、Home、Soft Limit、Tracking Error、Normal Stop、
Quick Stop 和 Fault Reset。`Quick Stop` 不能标成硬件 `Emergency Stop`。

## 7. Qt 技术路线

- 优先 Qt6 Widgets；工业调试台以表格、表单和状态面板为主，QML 暂无收益；
- Qt 作为 `RCR_BUILD_QT_DEVICE_WORKBENCH=OFF` 的可选构建；
- OFF 时核心 Runtime、daemon、tests 和 simulator 的依赖与行为不变；
- ON 时仅 Workbench target 查找并链接 Qt6 Core/Widgets；
- 不做隐式 Qt5 fallback，避免维护两套未经验证的构建矩阵；
- 第一阶段不引入 Qt Charts，趋势图可用轻量自绘或延后；
- 不为了展示 `QThread` 强行创建线程。

当前普通 shell 可发现系统 Qt5；Qt6 CMake 配置存在于本机 `robot_arm` Conda 环境，版本
6.11.1。是否能完成本项目 Qt6 构建仍是 **not_run**，进入 UI Phase 后再用固定环境验证。

### 7.1 时间与线程模型

- CAN fd 的阻塞等待、decode 和 session 生命周期不在 UI 线程执行；
- worker 生产有界 frame/telemetry snapshot；UI 通过 signal/slot 接收状态变化；
- UI 用 `QTimer` 以 10～20 Hz 刷新可见 snapshot，不能驱动设备闭环；
- CAN receive、Mock update 与 UI refresh 周期解耦；
- 原始 frame buffer 固定容量，暂停只暂停绘制，不停止接收和监督；
- 只有出现必须长期阻塞的 I/O owner 时才采用 worker-object + `QThread`；
- 所有 QObject 的创建、使用与销毁必须遵守 thread affinity。

## 8. Headless Test Runner MVP

Test Runner 是定位纠偏后的第一优先级。它必须先在无 GUI 条件下可测试，Qt 只负责启动、
停止和展示。

### 8.1 最小数据模型

```text
TestCase
  id / name / profile / parameters / timeout / criteria

TestRun
  run_id / case_id / backend / DUT / start / end / status

Measurement
  name / unit / value / timestamp / quality

TestResult
  PASS | FAIL | ERROR | ABORTED | SKIPPED
  criteria / measurements / diagnostics / cleanup_status
```

第一阶段用明确的 C++ case 定义，不引入 YAML/JSON DSL、脚本引擎或图形流程编辑器。
固定用例在接口和生命周期稳定后再考虑数据驱动参数。

### 8.2 每个用例的生命周期

```text
Prepare
  → Validate preconditions
  → Acquire session ownership
  → Execute stimulus
  → Wait and sample
  → Evaluate explicit criteria
  → Cleanup to known state
  → Persist result
```

任何 timeout、用户停止、decode error 或判定失败都必须进入 Cleanup。Cleanup 失败单独记录，
不能用原测试 PASS 覆盖，也不能自动重放旧命令。

### 8.3 首个真实端到端用例

首个 MVP 不伪造 motor velocity step test，而是复用已存在协议与模拟节点：

> **CAN Communication Health Test — vcan Node Simulator**

步骤：

1. composition/test fixture 启动 `rcr_node_sim` 和唯一的 `RuntimeDaemon`；
2. Workbench 验证 application snapshot 的 evidence 为 `VCAN`；
3. Runtime 独占打开 `vcan0`，Workbench 在固定窗口内采样 Runtime 已 decode 的快照；
4. 统计有效帧数、decode error、最大 heartbeat age 和时间窗口；
5. 按明确阈值判定 PASS/FAIL；
6. 返回内存 `TestResult`；调用方可用 `ResultWriter` 原子写入 JSON/CSV。

这条路径在有权打开 `vcan0` 的环境中，可以证明 Linux SocketCAN、CAN codec、simulator、
Runtime 监督、应用快照、采样、判定和结果文件的软件纵向闭环。它不能证明物理 CAN、
电气质量、真实 MCU heartbeat 或 actuator 响应。

第二个候选用例是现有 CAN V1 `OutputCommand → OutputStatus ACK` 往返测试；它只能叫
command/ACK protocol test，不能包装成 motor test。

### 8.4 Actuator 用例的进入条件

以下条件全部具备后，才能新增 `Motor Velocity Step Test`：

- 冻结的 actuator command/telemetry 线级合同；
- 明确单位、字节序、sequence、deadline 和 endpoint lease；
- 一个可复现的 simulator 或真实 MCU 实现；
- velocity/position quality 和 timeout 定义；
- Stop/Quick Stop/cleanup 可验证；
- 结果明确标注 Mock、vcan 或 physical backend。

## 9. Diagnostics 与 Runtime Fault 的边界

`Runtime Fault` 是控制权限和输出安全降级的一部分；`DiagnosticEvent` 是工程观察证据；
`TestResult::FAIL` 是某个测试标准未满足。三者不能合并成一个错误枚举。

```text
DiagnosticEvent
  timestamp
  source: COMMUNICATION | DEVICE | TEST | WORKBENCH
  severity: INFO | WARNING | ERROR
  code
  message
  run_id (optional)
  structured_context
```

规则：

- Workbench 可以显示 Runtime fault，但不能清除或覆盖其 authority；
- decode error、结果文件写入失败等 Workbench 诊断不自动升级为 Runtime fault；
- Runtime `CommLoss` 可以使测试失败，但 Test Runner 只记录和停止，不另造平行安全状态机；
- fault injection 默认关闭，并与正常命令入口分离；
- simulator 注入的 timeout/fault 必须标记 `TEST / MOCK`。

## 10. 结果与证据

第一阶段只输出固定 schema 的 JSON 和扁平 CSV，不引入数据库、PDF 或 HTML report engine。

冻结 schema id：`rcr.workbench.result.v1`。JSON 是完整证据；CSV 是一行索引。

每次结果至少记录：

- `run_id`、case id/version、开始/结束单调时间与墙钟时间；
- git commit、dirty/clean、build type；
- backend evidence label：`MOCK`、`VCAN`、`PHYSICAL`；
- interface、DUT/profile identity、参数和 criteria；
- measurements、诊断事件和最终 result；
- timeout/abort/failure reason；
- cleanup status；
- 环境能力不足时的 `permission_denied`、`unsupported` 或 `not_run`。

写文件采用同目录临时文件 + `fsync` + `rename`。已存在的最终文件拒绝覆盖。
`.tmp` 不是结果；读者只能把最终 `.json`/`.csv` 当作完整记录。

`ResultWriter` 对 FAIL/ERROR 强制要求 reason、至少一条 criterion、一条 measurement 和一条
diagnostic。Runner 在 Evaluate 前失败时会补齐生命周期证据，避免半份 ERROR 无法复核。
provenance 由调用方填入；库不 `popen(git)`，未知 commit 按 dirty 处理。

## 11. 增量文件计划

Phase 1 已落地（现按 4.2 分层存放）：

```text
linux/include/rcr/workbench/application/
  application_model.hpp
  runtime_application_adapter.hpp
linux/include/rcr/workbench/services/
  test_runner.hpp

linux/src/workbench/application/
  runtime_application_adapter.cpp
linux/src/workbench/services/
  test_runner.cpp

linux/tests/workbench/
  test_workbench_runtime_adapter.cpp
  test_workbench_runner.cpp
```

Phase 2 已落地：

```text
linux/include/rcr/workbench/services/
  can_health_test.hpp

linux/src/workbench/services/
  can_health_test.cpp

linux/tests/workbench/
  test_workbench_can_health.cpp
  test_workbench_can_health_vcan.cpp
```

Phase 3 已落地（诊断事件复用 `application_model.hpp`，结果类型留在 `test_runner.hpp`，
不为对齐草案文件名再拆一层）：

```text
linux/include/rcr/workbench/services/
  result_writer.hpp

linux/src/workbench/services/
  result_writer.cpp

linux/tests/workbench/
  test_workbench_result_writer.cpp
```

以下是后续阶段候选，不表示现在创建；出现重复职责时优先合并到现有模型，并放入对应层目录：

```text
linux/include/rcr/workbench/services/
  diagnostic_event.hpp   # 仅当与 application_model 诊断事件真的分家时
  test_case.hpp
  test_result.hpp

linux/tools/qt_device_workbench/
  CMakeLists.txt
  app/main.cpp
  controller/workbench_controller.hpp/.cpp
  ui/main_window.hpp/.cpp
  ui/presentation_models.hpp/.cpp   # 仅当 Widgets 不够用时再加

docs/workbench/
  README.md
  DEVELOPMENT_PLAN.md
  QT_DEVICE_WORKBENCH.md
  QT_WORKBENCH_NOTES.md
  ACTUATOR_COMMISSIONING_PROFILE.md
```

实际实现前先检查能否复用已有 `Result`、CAN codec、evidence writer 和进程管理方式；
不为对齐这份文件名草案而复制现有职责。

## 12. 分阶段计划与 Gate

### Phase 0 — Audit（本次完成）

- 审计目录、CMake、Runtime、CAN、supervision、fault、trace 和 tests；
- 审计 Qt/QML/Serial/chart/model/controller/Test Runner 是否存在；
- 冻结定位纠偏和证据边界。

Gate：本文与当前代码事实一致，未写实现代码。

### Foundation T0 — Headless Test Contract（先行本地实现完成）

- 冻结 TestCase/TestRun/Measurement/TestResult 最小合同；
- 定义取消、timeout、cleanup 和 session ownership；
- 写 evaluator 与生命周期单元测试。

Gate：不依赖 Qt，可确定性测试失败、取消和 cleanup。

当前实现：

- `rcr_workbench` 是独立无 Qt library，单向依赖 `rcr`；
- `TestRunner::run()` 同步执行，不私自创建线程；
- 固定 C++ callbacks 表达 Prepare / Execute / Evaluate / Cleanup；
- `CLOCK_MONOTONIC` 默认时钟可由测试注入；
- 取消请求使用短 mutex + atomic 状态完成跨线程握手；
- 阶段边界检查取消和 deadline，长等待 callback 仍必须主动轮询 context；
- Prepare 开始后的所有显式失败路径均执行 Cleanup；
- Cleanup 失败不能保留 PASS；
- 单元测试覆盖 PASS、criteria FAIL、prepare error、cancel、timeout、cleanup error、
  invalid contract 和 concurrent busy。

Runner 本身没有实现 CAN session ownership，不能把单实例 busy 误写成 CAN interface 已获得
独占锁。Phase 2 最终选择只读 Runtime-connected 测试；未来 Direct CAN command session 的
跨进程 lease 仍明确延期。

### Phase 1 — Runtime / Qt Architecture Boundary（本地实现完成）

- 增加无 Qt、无 Runtime 内部类型的 application command/telemetry/feedback/diagnostic DTO；
- 增加具体的 `RuntimeApplicationAdapter`，不创建只有一个实现的 service/interface/factory；
- 只暴露 activate/deactivate/clear fault/current digital output use case；
- 将 `DaemonSnapshot` 映射为低频可消费的 `RuntimeTelemetrySnapshot`；
- evidence class 由调用者显式传入，不从 `vcan0`/`can0` 名称推断；
- 用无 CAN、无 root 的单元测试验证映射、输入拒绝和 Runtime admission 转发；
- 保持 Runtime Core、daemon、CLI 和已有测试无 Qt 依赖。

Gate：Qt 未来只依赖 Workbench application model/adapter；没有 Qt → SocketCAN 直接路径；
Runtime 可继续 headless build/run。当前 Gate 不包含进程级隔离、Qt target 或 vcan 端到端命令。

### Phase 2 — CAN Health End-to-End（实现完成；软件 Gate 分环境记录）

- 通过现有 RuntimeDaemon 复用 SocketCAN、CAN V1 codec 和 `rcr_node_sim`；
- 完成固定观察窗口的 runtime-connected vcan health case；
- 明确 `VCAN` evidence label，并保持 Runtime 是唯一 CAN fd/thread owner；
- 不创建 generic Transport。

Gate：一条命令可复现 Prepare 到内存 `TestResult` 的完整链路；文件持久化由 Phase 3 承担。

| Gate 项 | 当前状态 | 证据边界 |
|---|---|---|
| 确定性判定与生命周期单元测试 | `pass` | 普通 Linux 用户态；不打开 CAN |
| 主机 `vcan` Runtime→simulator 软件纵向链路 | `pass`（local dirty-tree） | 23/23 CTest 所在主机构建；不是正式 clean evidence |
| 当前受限沙箱复跑 | `permission_denied` | 无权打开 `PF_CAN`，不能记作 pass/failed |
| clean-commit 可归档软件证据 | `pass` | commit `cf5892e`；见 portfolio 摘要与本机原始 artifact |
| physical CAN / MCU / actuator | `not_run` | 不属于当前软件 Gate |

因此 Phase 2 的实现、主机 vcan 软件 Gate 和 clean-commit 软件证据 Gate 已关闭；它们仍
不能合并成“已完成物理 CAN 验证”。

当前实现与原草案有一处有意调整：没有建立 Direct CAN bench session。Linux 允许多个 CAN
socket，但“能同时打开”不等于“拥有唯一命令 authority”；在跨进程 lease/锁合同尚未定义时，
另开 Workbench SocketCAN 会引入 `rcrd` 与测试工具同时控制节点的风险。本阶段因此只读采样
`RuntimeApplicationAdapter` 快照。测试同步运行在调用线程，以 `sample_interval` 等待；Runtime
的 scheduler/I/O 线程不向 UI 高频 push 数据。

判定覆盖 heartbeat 增量、最大 heartbeat age、设备在线连续性、decode reject、event queue
reject/drop、Runtime fault、communication/device latch 和 CAN I/O stop reason。指标未达阈值为
`FAIL`；Runtime 未运行、evidence 不匹配或计数器倒退使观察窗口无效时为 `ERROR`；取消为
`ABORTED`。`VCAN` measurement quality 固定为 `Simulated`。

当前受限沙箱无法打开 `PF_CAN`（`Operation not permitted`），所以这里复跑时 vcan 纵向目标
显式 `Skipped`；主机权限环境已有 local dirty-tree vcan PASS。沙箱 Skip 不能覆盖主机 PASS，
主机 PASS 也不能冒充 clean evidence、physical CAN 或真实设备证据。

### Phase 3 — Diagnostics & Results（本地实现完成）

- 加入 communication/device/test 诊断记录；
- 固定 JSON/CSV schema 和原子写入；
- 加入 timeout、malformed frame、abnormal heartbeat 的模拟注入测试。

Gate：每个 FAIL/ERROR 都有 criteria、measurement、reason 和 cleanup 证据。

状态：本地确定性实现与测试为 `pass`；commit `cf5892e` 上的 clean-commit 结果文件证据为
`pass`，见 `evidence/portfolio/workbench_phase3_5_20260811.md`。

当前实现：

- `DiagnosticSource` 增加 `TEST`；`DiagnosticEvent` 增加可选 `run_id`/`context`；
- `TestRunContext` 可在一次 run 中追加 measurement、diagnostic、parameter 和环境身份；
- `TestRunner` 对 FAIL/ERROR 封口：缺 criteria/measurement/TEST diagnostic 时补齐生命周期证据；
- `ResultWriter` 写出 `rcr.workbench.result.v1` JSON 和一行 CSV；同目录 `.tmp` + `fsync` +
  `rename`；拒绝覆盖已有最终文件；
- CAN Health 在心跳停滞/过旧、离线、非法帧和失败 criterion 上分别记录 COMMUNICATION/
  DEVICE/TEST 诊断；
- 确定性注入覆盖 timeout、malformed frame、stale heartbeat；有 `vcan0` 时再用模拟器
  `--fault-stop-heartbeat` 与 `--fault-send-illegal-after-ms` 对照。

写入失败是 Workbench 诊断，不升级为 Runtime fault。Phase 3 当时还没有 Qt Results 页；
Phase 4 已加上路径展示。长期诊断历史服务仍没有。

### Phase 4 — Optional Qt6 Workbench（clean-evidence Gate 已关闭）

- 增加可选 CMake 开关；
- 先实现 Overview、Tests、Diagnostics、Results 和 CAN Monitor；
- UI 只调用已经验证的 headless service；
- 验证退出和异常关闭顺序。

Gate：Qt OFF 无回归；Qt ON 可完成同一个 CAN Health Test，结果与 headless 一致。Qt Gate
不能借用 Phase 2/3 的 local dirty-tree 结果冒充自己的 UI/线程/退出验证。

当前实现采用 Widgets + Controller + worker object：100 ms `QTimer` 只读 snapshot；同步 CAN
Health 和 ResultWriter 位于 `QThread`。`app/main.cpp` 暂时是同进程 Runtime composition
root，故仍不能声称进程级 crash isolation。`RCR_BUILD_QT_DEVICE_WORKBENCH` 默认 OFF，
Qt 不进入 core。当前页是 Overview / Actuator 01 / Tests / Diagnostics / Results；
CAN Monitor 仍未实现。

clean commit `834ec899` 上的 Qt OFF 与 Qt ON 全量 CTest 均为 23/23 `pass`；Qt6 6.4.2
offscreen health 也为 `pass`，结果明确记录为 `VCAN` / `SIMULATED`。详细合同见
[Qt Workbench](../README.md)，证据摘要见
[Phase 4 clean evidence](../../../evidence/portfolio/qt_workbench_phase4_20260811.md)。

### Phase 5 — Actuator 01 Profile（A0/A1/A3 local pass；A2 open）

- 先冻结模拟或实物 actuator contract；
- 再引入 Enable、Jog、Home、Soft Limit、Tracking Error 和 Quick Stop；
- 评审 Runtime command admission、authority lease 和 endpoint lease；
- Mock 与 physical evidence 分开。

Gate：不能用数字输出 mailbox 伪装运动命令；Qt 崩溃/失联后的停止责任已定义并验证。

当前切片明确为 `MOCK / ISOLATED`：新增纯 C++ deterministic profile、13 个 headless 场景和
Qt Actuator 01 页；Qt offscreen Enable→Home→Start→Quick Stop 为 `pass`。Jog 使用 token、
200 ms deadman 和 2 s 最大连续时长；release 丢失不会无限续租。Qt OFF/ON 全量 CTest 均为
24/24，Workbench ASan/UBSan 为 6/6。

A2 Runtime command domain/admission 未实现，Mock 没有发送 CAN motion frame，不能把当前结果
描述成 Runtime-integrated actuator、物理 servo 或 Qt crash containment。由于进入本 Phase 前
已有用户文档改动，当前仅为 dirty-tree local evidence，formal clean Gate 仍打开。

### Phase 6 — Documentation & Demo

- 更新 README、架构图、build/run/test 命令；
- 在 `docs/KNOWLEDGE_BASE.md` 增加 Qt event loop、worker ownership、Test Runner、
  SocketCAN 证据链等知识卡；
- 录制或保存可复现实验输入和输出。

当前：源码与文档已按 Workbench 新架构收入 `linux/.../workbench/{application,services,profile}`
和 `docs/workbench/`；知识卡 §6.14 / §10.17–10.19 与模块卡 36–42 已存在。仍缺人工视觉
验收录像和 Phase 5A clean evidence 目录。

Gate：所有声明都能映射到测试、vcan、Orange Pi 或物理设备的具体证据。

## 13. Actuator Profile 的未来安全边界

之前规划的 Actuator Console 不被删除，而是降为 Workbench 的设备 profile。进入 Phase 5
时至少重新评审：

- Runtime 只有一个权威 state machine，不能在 Qt 内复制；
- motion-authorizing 命令必须携带 session、单调 sequence 和 deadline；
- 离开 Active、Fault、Hold 或新 session 必须使旧 admission/target 失效；
- Jog 必须同时有 press/release、deadman timeout、Stop 和 fault 路径；
- Linux authority lease 与未来 MCU endpoint output lease 是两层不同责任；
- Normal Stop 是受控减速请求，Quick Stop 是更快的软件停止策略；两者都不是硬件 E-stop；
- following/tracking error 必须定义单位、阈值、持续周期和 safe transition；
- Qt 退出后不能依靠按钮 release 作为唯一停止条件。

如果这些条件不能以小改动接入当前 Runtime，先只做独立、明确标注的 Mock profile，
不得创建第二套平行 Runtime 或声称完成了安全集成。

本节只是边界摘要；实施依据以
[Actuator 01 Commissioning Profile 详细设计](../ACTUATOR.md)
为准。定位纠偏不构成删除该 profile 既有工程细节的理由。

## 14. 明确延期

- Serial/QSerialPort：RS485/Modbus RTU 已进入正式通信 roadmap，但没有真实 DUT/寄存器合同，
  本 Phase 不实现；
- Qt Charts：先证明测试数据链，再决定轻量绘图；
- Runtime IPC：保留目标边界，不在 Workbench MVP 大改 daemon；
- 多设备并发测试和资源调度；
- Test DSL、脚本引擎、插件系统、数据库、PDF/HTML 报告；
- 真实 actuator CAN 协议、STM32 固件和 1 kHz control；
- two-axis coordinated motion / interpolation；
- FPGA、芯片验证和实验室仪器集成。

## 15. 停止规则

出现以下任一情况时停止扩展并回到设计评审：

- Workbench 开始拥有 Runtime 全局状态或安全 fault authority；
- `MainWindow` 开始包含 CAN loop、设备状态机或测试判定；
- 为不存在的 Serial/FPGA/CNC backend 创建通用框架；
- Qt 成为 core Runtime 的必需依赖；
- `rcrd` 与 Direct Workbench 同时控制同一 CAN 节点；
- Test FAIL 被宣传为 Runtime fault，或 Mock/vcan 被宣传为实物验证；
- 为实现 UI 先改动大量 Runtime；
- 未定义 cleanup 就增加新的刺激命令；
- 新功能无法回答一个具体、可重复的设备测试问题。

## 16. 下一步建议

Phase 5A isolated Mock 已实现并完成 local regression；下一步不是扩展更多 UI，而是先处理现有
dirty worktree，再决定关闭 clean evidence 或单独评审 A2 Runtime admission。

Phase 2/3 的 formal clean-evidence Gate 已在 commit `cf5892e` 上关闭。Phase 4 作为独立工作包
开始后会再次使工作树 dirty；Qt Gate 必须独立验证，不能自动继承 headless evidence。

Direct CAN command session、A2 Runtime admission、Serial/Modbus RTU、真实 MCU 和 physical
CAN 仍不进入当前切片。尤其在跨进程 authority/lease 未定义前，不增加第二个可写 CAN owner。
