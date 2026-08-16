# Code ownership map

本文是代码职责归属的单一入口。系统组件关系见 [ARCHITECTURE.md](ARCHITECTURE.md)，daemon
生命周期见 [RCRD_CONTRACT.md](RCRD_CONTRACT.md)。这里的分区是责任边界，不要求严格逐层
调用；`linux/CMakeLists.txt` 是实际 source/target 清单的权威来源。

## Dependency direction

```text
protocol/core building blocks ─┐
Linux mechanisms ──────────────┼─> Runtime semantics / supervision ─> RuntimeDaemon/apps
CAN codec/node logic ──────────┘                                      │
                                                                      v
                                                             rcr_workbench
                                                                      │
                                                                      v
                                                               optional Qt UI
```

Qt UI 默认关闭，只消费 Workbench/Runtime 公共能力。它不拥有 Runtime Core、CAN fd 或
supervision/safety 状态逻辑。

## Ownership table

| Module | Responsibility | Owns | Must not own | Depends on | Used by |
|---|---|---|---|---|---|
| `linux/src/core` | 无 Linux I/O 的可测试 Runtime building blocks | queue/mailbox/state/watchdog/trace/stats 的数据结构与局部规则 | fd、线程启动、CAN socket、进程退出 | C++ standard library、公共类型 | `runtime`, `supervision`, tests |
| `linux/src/can` | CAN V1 codec 与节点纯逻辑 | 固定宽度 wire encode/decode、DLC/字节序校验、node state transition | SocketCAN fd、线程、Qt、物理执行声明 | `protocol/can_v1`, public CAN types | `daemon`, `sim`, Workbench adapter, tests |
| `linux/src/linux` | POSIX/Linux mechanisms | fd RAII、clock、scheduler primitive、epoll、eventfd/signalfd、SocketCAN、`CanIoLoop` | Runtime 恢复策略、UI state、systemd policy | Linux/POSIX API、CAN codec where needed | `runtime`, `daemon`, apps, tests |
| `linux/src/runtime` | 活的 Runtime 语义组合 | state/watchdog/mailbox/trace/ACK 的实例与原子事务、周期监督策略 | SocketCAN fd、进程退出码、systemd、Qt | `core`, scheduler/time | `supervision`, `daemon`, Workbench adapter, tests |
| `linux/src/supervision` | 节点/设备会话监督 | heartbeat/session/restart 解释、CommLoss 与持久恢复 blocker 决策 | daemon 生命周期、fd、UI、协议字节编码 | `core`, `runtime`, CAN domain types | `daemon`, tests |
| `linux/src/daemon` | 进程内 composition root | Runtime/supervisor/I/O worker 装配、停止顺序、snapshot 与错误汇总 | 新 wire contract、重复状态机、Qt presentation | `linux`, `runtime`, `supervision`, `can` | `rcrd`, `rcr_cell_app`, Workbench adapter, acceptance tests |
| `linux/src/sim` | 独立 CAN node simulator | simulator event loop、timer/fault injection test surface | production safety 或真实硬件结论 | `can`, Linux fd/time | `rcr_node_sim`, tests |
| `firmware/stm32f103` | 独立物理 CAN V1 peer | Cortex-M 启动、bxCAN/SysTick/IWDG、ISR 队列、MCU codec/node state、PC13、PA8/TIM1、PA0 对射去抖 → `input_bits` bit0 | Linux Runtime 状态、SocketCAN fd、CellReady、Modbus DO0、连续角度、功能安全声明 | `protocol/can_v1` 线级合同、STM32F103C8T6 寄存器 | Orange Pi physical-CAN 闭环演示 |
| `linux/src/workbench/application` | Runtime-facing commissioning use cases | Runtime adapter、稳定 DTO；`CellReadyMapper`；CEL1 `cell_app_protocol`；Remote loopback frame/DTO | Qt widgets、CAN fd、Runtime policy、直接写线圈、私有 daemon 结构上网 | `rcr` public Runtime capability | Workbench services、`rcr_cell_app`、Qt controller、tests |
| `linux/src/workbench/services` | 可复用 headless diagnostics workflow | test runner、CAN health、result writing；Remote loopback endpoint/client；Physical Modbus RTU service / POSIX serial / TCP agent；`CellAppClient`/`Server` | Runtime state machine、Qt event loop、SocketCAN fd、万能 Transport | Workbench application/profile、标准库、POSIX serial/socket | Qt controller、headless tests、`rcr_modbus_rtu_agent`、`rcr_cell_app` |
| `linux/src/workbench/profile` | 隔离配置与 MOCK actuator / Modbus profile | profile validation/defaults、Mock-only identity | 实物 actuator control、CAN motion frame、真实串口、安全声明 | 公共 Workbench types | services、Qt UI、tests |
| `linux/tools/qt_device_workbench/controller` | Qt 与非 Qt use case 的适配 | QObject、signal/slot、UI-facing async orchestration；`--cell-peer` 只读 CEL1 | CAN fd、Runtime state ownership、CellReady 决策、DO0 自动写线圈 | `rcr_workbench`, Qt Core | Qt UI |
| `linux/tools/qt_device_workbench/ui` | 可选工程界面 | widgets、presentation state、human-triggered actions | supervision/safety decision、周期线程、CAN ownership | Qt controller、Qt Widgets | commissioning engineer |

## Cross-boundary rules

- `RuntimeDaemon` 可以直接组合 Linux、Runtime、CAN 与 supervision；这不把它们变成严格
  OSI 层，也不授权 daemon 重写各模块规则。
- `NodeSupervisor` 使用具体 `LinuxRuntime` 直到出现第二个真实 Runtime/caller；不为假想扩展
  新建接口。它保留最后一次 `input_bits` / `last_output_mirror` 供观察；到位不是 Fault。
  CellReady 由 Workbench `CellReadyMapper` 拥有，不进 Runtime Core。
- 演示拓扑下 `rcr_cell_app` 与 Runtime 同进程，是该进程内唯一 CAN owner；不要并行再跑 `rcrd`。
  `rcrd` 是同一 `RuntimeDaemon` 的 standalone host（vcan / systemd），不是第二套 Runtime。
  ThinkPad Qt `--cell-peer` 不拥有 can0。本机 `--can vcan0` 对照仍可同进程。
- Workbench 的依赖方向固定为 `rcr` → `rcr_workbench` → optional Qt。Actuator Mock
  在 profile；Physical Modbus 在 services（板上 agent 拥有 tty）。默认 build 不需要 Qt。
- `protocol/` 只拥有 wire contract；Linux C++ 类型不能直接成为 MCU ABI。
- STM32 固件只复制冻结 wire 数值和逐字段 codec，不包含 Linux C++ 头，也不进入
  `linux/CMakeLists.txt`；两端一致性由 golden bytes 和物理帧验证。
- `experiments/` 的 Modbus、EtherCAT 与 multibus observer 不进入以上 Runtime ownership，除非
  独立 Gate 关闭并重新评审接口。
