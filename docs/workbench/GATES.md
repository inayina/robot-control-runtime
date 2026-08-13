# Workbench 合同与 Gate

当前怎么跑、目录在哪：见 [README.md](README.md)。  
本文只保留**还有效的合同**和**门开没开**。阶段日记在
[archive/PHASE_HISTORY.md](archive/PHASE_HISTORY.md)。

## 它回答什么

> 某个底层设备通不通、按不按合同响应、失败证据在哪个文件里？

首个设备场景叫 Actuator 01，细节在 [ACTUATOR.md](ACTUATOR.md)。工作台本身不能因此变成
CNC 或伺服产品。

## 谁拥有什么

本仓继续拥有：Runtime 状态机、watchdog、supervision、fault、scheduler、trace、
SocketCAN、节点模拟器、headless 测试链。

Workbench 只负责：展示 backend/DUT、发受约束的请求、跑已有测试、把结果写成文件、
标清 `MOCK` / `VCAN` / `PHYSICAL`。

Workbench 不拥有：全局状态权威、1 kHz 闭环、Dashboard、ROS 2、G-code、功能安全急停。

## 冻结合同

1. 只有一条真实 CAN 路径时，不建 `ITransport` / `IDevice` / 插件。
2. `RuntimeDaemon` 是唯一可写 CAN owner。Health 只读 `RuntimeApplicationAdapter` 快照。
3. Direct CAN bench、跨进程 lease、第二个可写 socket：**延期**。没关这些门之前不加。
4. `Runtime Fault` ≠ `DiagnosticEvent` ≠ `TestResult::FAIL`。写文件失败不升级成 Runtime fault。
5. 结果 schema：`rcr.workbench.result.v1`。同目录 `.tmp` + `fsync` + `rename`。FAIL/ERROR
   必须带 reason、criterion、measurement、diagnostic、cleanup。
6. Qt 可选，`RCR_BUILD_QT_DEVICE_WORKBENCH` 默认 OFF；不做 Qt5 fallback。
7. Mock / vcan / 实物必须分开记。vcan 不是物理 CAN。

## 页面：现在有 vs 愿望

| 有 | 没有（别写成已实现） |
|---|---|
| Overview / Connection(LOOPBACK) / Actuator 01 / Modbus I/O / Tests / Diagnostics / Results | Devices 市场、Live 曲线、CAN Monitor、Manual 独立页、UDP telemetry |

## Gate

| 门 | 状态 | 不能夸成 |
|---|---|---|
| T0 TestRunner 生命周期 | 关 | CAN 独占锁 |
| P1 Adapter / DTO | 关 | 进程隔离 |
| P2 Runtime-connected CAN Health | 关（`cf5892e`） | 物理 CAN |
| P3 结果 schema / 原子写 | 关（`cf5892e`） | 长期诊断库 |
| P4 可选 Qt + offscreen Health | 关（`834ec899`） | 人工视觉验收、crash isolation |
| P5A A0/A1/A3 Mock + UI | local pass | clean Gate、Runtime 已接入运动 |
| P5 A2 Runtime admission | **开** | — |
| P5 A4 actuator CAN 合同 | **开** | — |
| P5 A5 实物 | **开** | — |
| P6 演示录像 | **开** | — |
| M1 Modbus I/O headless Mock | 关（local/dirty，2026-08-13） | 真实 RTU / RS-485 |
| M2 Modbus I/O Qt presentation | 关（local/dirty，2026-08-13） | 真实 Remote I/O |
| Remote Workbench Boundary（loopback） | 关（local/dirty，2026-08-13） | 物理 PC–ARM、UDP、COMMAND、crash isolation 产品验收 |
| Direct CAN | **延期** | — |

本文件不单独决定当前任务。[Remote Workbench Boundary Gate](../plans/REMOTE_WORKBENCH_BOUNDARY_GATE.md)
已关闭；当前没有 Active implementation Gate。不启动真实 Serial/RTU、物理 PC–ARM、UDP、A2、
Direct CAN、EtherCAT 或 physical actuator。下一步必须回到
[plans/README.md](../plans/README.md) 选择。

## 延期（整表未做）

真实 Serial/QSerialPort/Modbus RTU、Qt Charts、多设备调度、Test DSL、数据库/PDF 报告、真实 actuator
协议 / STM32 / 1 kHz、两轴插补、FPGA 仪器。

## 停止规则

出现任一条就停下来重评：

- Workbench 开始拥有 Runtime 状态或安全 fault
- `MainWindow` 里出现 CAN loop、设备状态机或 PASS/FAIL 判定
- 为不存在的 Serial/FPGA/CNC 先做通用框架
- 把 Modbus Mock 的 placeholder 当成 MR0-IOR08 手册值，或为它修改 CAN overlay
- Qt 变成 core 必需依赖
- `rcrd` 和 Workbench 同时写同一 CAN 节点
- Test FAIL 被说成 Runtime fault，或 Mock/vcan 被说成实物
- 为做 UI 先大改 Runtime
- 没定义 cleanup 就加刺激命令
- 新功能回答不了一个可重复的设备测试问题
