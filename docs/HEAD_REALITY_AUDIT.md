# HEAD Reality Audit

> **Historical audit snapshot.** 本文固定 2026-08-18 的观察与证据边界，不声明当前 HEAD 或
> Current Gate。Post-Audit Local Development SPEC 后来完成 LD8 并关闭；当前项目状态和未来
> Gate 选择只读 [plans/README.md](plans/README.md)。

审计日期：2026-08-18  
Runtime 基线：`3c3bba419491cd6d833b9c55c42eab8aca9757d9`，`main`，审计开始时 clean  
Platform 对照：`49509bd234d2076bf4595574f1b330518bbb58ad`，`main`，审计开始时 clean  
角色：**只读事实快照 / 非 scope authority**  
审计当时的 Active Gate：
[Closed-Loop Portfolio Freeze Gate](plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)  
其后（同日用户选择 B）唯一 Current Gate 曾改为
[Post-Audit Local Development SPEC](plans/POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md)，该 SPEC 后来完成
LD8 并关闭。Freeze 仍为 `OPEN / DEFERRED`，不得由本文标 CLOSED。

落实状态：文档模型已落地。current-HEAD 生命周期 / epoll-shutdown / 普通 Linux CPU 样本已写入
`evidence/head_reality_audit/20260818T033609Z/`（`LOCAL / VCAN / DIRTY`）。isolated netns 为
`permission_denied`；host `vcan0` 仅在用户授权停止旧 `rcrd.service` 后使用。本批次未复现先前
同进程线程计数失败，不是“永远无泄漏”。审计实验本身结束即停；是否实施本机 SPEC 由用户另选。

本文回答当前提交真正实现了什么、进程与 Linux 资源怎样工作、证据能够支持什么，以及第一阶段
只应落实哪些学习与验证工作。范围仍以根 `SPEC.md` 为准；系统关系、代码 owner、daemon 合同和
Gate 分别以 `ARCHITECTURE.md`、`CODE_OWNERSHIP_MAP.md`、`RCRD_CONTRACT.md` 和
`docs/plans/README.md` 为准。本文不能因测试通过而关闭物理 Gate。

## 1. 方法与证据规则

本轮交叉检查：

- 当前 Git SHA、工作树、CMake target 和程序入口；
- `RuntimeDaemon`、`LinuxRuntime`、`NodeSupervisor`、`CanIoLoop`、Workbench 与部署脚本；
- 两个 fresh `/tmp` Debug build（Qt OFF / Qt ON）、CTest 和 Platform Go tests；
- 本机 `/proc`、systemd、vcan 与已安装 release 的只读状态；
- `evidence/README.md`、当前 Freeze Gate 及对应 closed-loop evidence。

事实分类：

- **Fact**：当前源码、测试输出或只读运行态直接支持；
- **Hypothesis**：有症状但原因尚未由实验区分；
- **Conclusion**：由 Fact 限定范围后作出的工程判断。

本机旧服务、vcan、dirty physical smoke、当前 HEAD 软件测试和 clean hardware acceptance 不得
互相升级。旧服务的 `/proc/<pid>/fd` 因权限拒绝未完成 live 枚举；FD 表因此是当前 HEAD 源码
inventory，不冒充旧 release 的 live observation。

## 2. 结论摘要

1. 当前主演示是 Orange Pi 上 `rcr_cell_app` + `rcr_modbus_rtu_agent`，ThinkPad Qt 以
   `--cell-peer` 连接；standalone `rcrd` 是同一 `RuntimeDaemon` 的替代宿主，不与主演示并行
   写 `can0`。
2. 默认 Runtime 核心为 main、周期、CAN I/O 三线程；watchdog 和 NodeSupervisor 不另建线程。
3. standalone `rcrd` 典型持有四个内部持久 fd：SocketCAN、epoll、eventfd、signalfd；周期线程
   使用绝对 `clock_nanosleep`，不使用 timerfd。
4. 核心 epoll 只处理 SocketCAN、内部 stop 和 SIGINT/SIGTERM。CEL1、Modbus TCP、串口和周期
   tick 不属于该 epoll 实例。
5. 本批次 `test_runtime_daemon` 20 轮与 `test_rcrd_process` 10 轮均通过（iface-down 按设计
   skip）。先前同进程线程计数失败本批次未复现；不能写成已证实泄漏，也不能写成永远无泄漏。
6. 用户授权后旧 `rcrd.service`（release `7bb994...`）已 stop 且保持 inactive，以便 current-HEAD
   独占 host `vcan0`。该停止不是 Orange Pi / physical CAN evidence，也未把旧 release 升级为
   current HEAD。
7. current-HEAD 的短时进程/FD/退出观察已有本机 vcan 证据；主演示部署边界和 Freeze Gate 第 11
   项 RS-485 掉线瞬间证据仍未关闭。软件实验不能替代物理 Gate。

详细进程和资源模型：

- [Runtime Process / Thread Model](RUNTIME_PROCESS_THREAD_MODEL.md)
- [FD / Event Model](FD_EVENT_MODEL.md)

## 3. 当前真实系统架构

```text
ThinkPad
  Qt Workbench --cell-peer
        │ CEL1/TCP
        ▼
Orange Pi
  rcr_cell_app                    rcr_modbus_rtu_agent
  ├─ RuntimeDaemon               ├─ TCP commissioning endpoint
  │  ├─ LinuxRuntime             └─ /dev/ttyS7 → Modbus RTU → MR0-IOR08
  │  ├─ NodeSupervisor
  │  └─ CanIoLoop → can0 → STM32F103 → SG90 / PA0
  └─ CellReadyMapper ────────────────→ DO0 requested/confirmed
```

Ownership 不变量：

- `rcr_cell_app` 是主演示 CAN owner；`rcrd` 是替代宿主；
- Qt `--cell-peer` 不拥有 Runtime、CAN fd、watchdog 或 DO0 自动闭环；
- Modbus agent 独占串口，但不决定 CellReady；
- STM32 拥有 PWM/GPIO/节点输出 lease；物理 MR0 拥有线圈真实状态；
- `robot-platform-service` 是独立管理面，当前没有 Runtime feed。edge-agent 的正常 heartbeat
  仍将 `runtime_state` 写成固定 `idle`，不能当作 Runtime observation。

## 4. Runtime 进程模型

| 进程 | 真实职责 | 主要阻塞点 | 当前生命周期合同 |
|---|---|---|---|
| `rcrd` | standalone Runtime 宿主、单 CAN 节点监督 | main 等停止；I/O epoll；周期绝对睡眠 | 有 systemd unit；不自动发送演示输出 |
| `rcr_cell_app` | 主演示 Runtime 宿主、CEL1、CellReadyMapper | main `poll(20 ms)`；同步 Modbus 边沿最多约 1 s | 手工启动；无已提交 unit |
| `rcr_modbus_rtu_agent` | TCP commissioning → physical RTU | accept/client/serial poll/read/write | 单线程无限循环；无显式 graceful stop |
| Qt Workbench | 工程站 UI、命令与状态展示 | Qt event loop、两个显式 worker | `--cell-peer` 不建本地 Runtime |
| `rcr_node_sim` | vcan 验收 peer | 自有 epoll/timerfd | 测试/验收，不是生产节点 |
| `platformd` / `edge-agent` | 管理面 API、SQLite、host metrics | Go runtime | 与 Runtime/CAN session 未集成 |

## 5. Runtime 线程模型

默认 `rcrd` / `rcr_cell_app` 核心：

```text
main/application thread
├─ PeriodicScheduler worker
└─ CanIoLoop worker
```

- main：装配、等待和停止；`rcr_cell_app` 还串行处理 CEL1 与 CellReady edge action；
- scheduler：`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`，随后执行 Runtime tick、watchdog、
  ACK timeout 与 NodeSupervisor budget；
- I/O：`epoll_wait` SocketCAN/eventfd/signalfd，decode 后投递有界队列，并泵出 mailbox；
- `--duration-ms > 0` 才有第四个 duration helper；
- Qt 代码显式创建两个 `QThread`。`--cell-peer` 为 UI + 两 worker；本地 Runtime 模式再增加
  scheduler/I/O。Qt 或系统库内部线程不由此静态计数。

## 6. FD inventory

### 6.1 standalone `rcrd`

| fd | owner | 模式 / 等待 | 关闭顺序 |
|---|---|---|---|
| stdin/stdout/stderr | launcher/systemd | 目标决定；不进 Runtime epoll | 进程退出 |
| PF_CAN RAW socket | `CanIoLoop::SocketCan` | nonblocking；LT IN/ERR/HUP | epoll DEL → close |
| epoll fd | `CanIoLoop::EpollReactor` | I/O worker `epoll_wait` | I/O join 后析构 |
| eventfd | `RuntimeDaemon` | nonblocking；epoll IN | write/drain → I/O join → close |
| signalfd | `RuntimeDaemon` | nonblocking；epoll IN | drain → workers join → close/恢复 mask |

没有 Runtime timerfd、pipe、共享内存、持久 trace 文件或独立 watchdog fd。启动时读取 sysfs 的
ifstream 是短寿命 fd，不属于 steady-state inventory。

### 6.2 宿主附加 fd

- `rcr_cell_app`：nonblocking CEL1 listen socket、最多八个 nonblocking client、一次 edge action
  的短连接 Modbus TCP socket；它们由 main 的 `poll`/同步调用处理，不注册进 Runtime epoll。
- Modbus agent：TCP listen、单 client、延迟打开并持有的 `/dev/ttyS7`；使用 `poll`，不使用
  epoll/eventfd/signalfd。
- node simulator：在自己的进程中使用 heartbeat/delay/duration/restart timerfd；它不能证明
  Runtime 使用 timerfd。

## 7. epoll event flow

CAN RX：

```text
CAN netdevice → PF_CAN receive queue → LT readiness
→ CanIoLoop epoll → nonblocking read 到 EAGAIN 或 budget 用尽
→ CAN V1 decode → BoundedInputQueue
→ 下一周期 NodeSupervisor 消费 → Runtime 状态 / fault / ACK 变化
```

命令 TX：

```text
Qt/CEL1 → rcr_cell_app main poll → Adapter → LinuxRuntime command admission
→ latest-wins mailbox → CanIoLoop 下一次 epoll 唤醒或 10 ms timeout
→ 重查 mode/session/deadline → SocketCAN write → OutputStatus ACK supervision
```

内部 stop：`request_stop → eventfd write → epoll → drain → I/O loop exit`。

周期 tick：`clock_nanosleep absolute deadline → on_tick`，不经过 epoll。

## 8. signal 与 shutdown

```text
systemd SIGTERM / terminal SIGINT
→ 信号已经在创建 workers 前被 block
→ signalfd readable
→ I/O worker 优先处理停止，DEL/close SocketCAN
→ main 观察 I/O stopped
→ join I/O
→ join optional duration helper
→ final summary
→ stop/join scheduler
→ reset I/O/supervision/runtime/queue
→ close eventfd/signalfd，恢复 main 原 signal mask
→ exit
```

`rcr_cell_app` 在退出前还 disconnect Modbus client、close CEL1 server。它若正处于最多约 1 s 的
同步 Modbus transaction，退出检查可能延后；这是需要实测的阻塞上界，不是已经确认的故障。

Modbus agent 没有 stop flag 或 signal-to-fd 路径。SIGTERM 只能依靠进程终止时由内核回收 fd，
不是同等级的受控关闭合同。

## 9. state ownership 与重启

| 状态 | owner |
|---|---|
| mode/fault/interlock、command session/sequence、watchdog、ACK、mailbox、trace | `LinuxRuntime` |
| node online/boot/session/heartbeat/input/fault 与 recovery latches | `NodeSupervisor` |
| CAN/epoll、I/O counters、未成功发送的 pending command | `CanIoLoop` |
| I/O→periodic 有界事件 | `BoundedInputQueue` |
| 进程装配、start/stop/exit 与聚合 snapshot | `RuntimeDaemon` |
| CellReady 与最近 DO0 requested/confirmed 投影 | `rcr_cell_app` |
| 实际 PWM/GPIO/output lease | STM32 |
| 设备身份、管理 session 与历史 | Platform；不是 Runtime/CAN session |

Runtime 进程重启会重建全部进程内控制状态、queue、trace、I/O stats、CEL1 clients 和 mapper；不会
恢复 mailbox、pending ACK 或重放旧命令。release/config/systemd/journal/evidence 与 Platform
自己的 SQLite 数据可以持久化，但不能恢复为 Runtime control authority。

## 10. 当前 evidence 状态

| 范围 | 2026-08-18 事实 | 允许表述 |
|---|---|---|
| Qt OFF fresh build | `/tmp/rcr-head-audit-F4r3nl` Debug Qt-OFF 构建成功 | 当前构建可用于本机软件实验 |
| lifecycle A `test_runtime_daemon` | until-fail:20 全部 Passed；每轮 11 passed + 1 skip | 本批次未复现；不是“无泄漏证明” |
| lifecycle A `test_rcrd_process` | until-fail:10 全部 Passed | 独立进程启停本批次通过 |
| B epoll/eventfd/signalfd | duration 与 SIGTERM 顺序与模型一致；`strace -p` attach `permission_denied` | syscall/关闭顺序；非 latency |
| C ordinary Linux CPU | 5 baseline + 5 stress-ng CPU，`SCHED_OTHER`，exit 0 | 本机 powersave 样本；非硬实时 |
| isolated netns | `permission_denied` | 未绕过；host vcan0 在授权停旧服务后使用 |
| Qt ON fresh build | 35/35 CTest process entries passed，含 vcan/Qt | current-HEAD 本机软件路径 |
| Platform | host loopback 环境 `go test ./...` 通过 | 独立管理面本地实现；非 Runtime integration |
| 本机 systemd | 旧 `rcrd.service` 已授权 stop，ActiveState=inactive | 旧 release 不再占用 vcan0；未重启 |
| closed-loop physical | `c0d793...` dirty：1–10、12–13 pass；11 未跑；无运动录像 | 受限 dirty physical smoke；Gate 仍 partial |
| current HEAD physical | 本轮未跑 | NOT RUN |
| realtime / safety | 无 hard realtime / functional-safety evidence | 只能称普通 Linux 机制与受限测量 |

本批次生命周期分类：

- Fact：`DaemonRepeatStartStopFdAndThreadStable` 曾在更早的 Qt-OFF 首轮失败；本批次 20 个外层
  `test_runtime_daemon` 与 10 个 `test_rcrd_process` 均通过，iface-down 按设计 skip。
- Hypothesis：那次失败仍可能是真实回收延迟、测试进程其他线程或 `/proc` 时序，本批次未再区分。
- Conclusion：本批次未复现，不改 C++；不得写成永远无泄漏。

## 11. Deploy / Observe / Diagnose / Verify gap

| 面 | 已有 | 当前 gap | 第一动作 |
|---|---|---|---|
| Deploy | SHA release、`current` symlink、standalone rcrd unit、dry-run rollback | install script 不打包主演示；cell app/agent 无 unit；current HEAD 未部署 | 不扩框架；先记录 topology 与实际需求 |
| Observe | snapshots、final summary、journal、CEL1/Qt | standalone trace 无稳定外部出口；Platform 固定 idle；缺 current SHA/uptime/restart 汇总 | `/proc` + journal + strace 观察实验 |
| Diagnose | CTest、vcan acceptance、fault matrix、历史 evidence | lifecycle symptom 未解释；无 current incident bundle/timeline | lifecycle 重复与 shutdown trace |
| Verify | current software test + historical physical slices | current HEAD 无 board/systemd/physical rerun；无 current long-run CPU/memory | 普通 Linux pressure baseline；物理仍按 Gate |

## 12. 优先学习的三个 OS 机制

1. **epoll + nonblocking + EAGAIN**：解释 readiness、budget、公平性和非忙等。当前一个 I/O
   worker 已覆盖三类事件，不选 thread-per-fd 或 busy polling。
2. **signalfd + eventfd + join/close order**：把外部信号和内部 stop 纳入 fd 等待，并避免 async
   signal handler 的受限执行环境。self-pipe 可行，但当前 Linux-only daemon 无第二种真实需求。
3. **CLOCK_MONOTONIC + absolute sleep**：统一 watchdog/deadline/heartbeat，并避免相对 sleep
   累积漂移。timerfd 更适合把周期并入 fd loop；当前 scheduler 已有独立 worker，不为展示改造。

## 13. 首轮真实实验（已执行）

证据目录：`evidence/head_reality_audit/20260818T033609Z/`。class=`LOCAL / VCAN / CURRENT-HEAD / DIRTY`。

### 13.1 Lifecycle

- Fact：`ctest --repeat until-fail:20 -R '^test_runtime_daemon$'` exit 0，每轮 ~5.90 s，
  `DaemonVcanInterfaceDownPropagatesIoError` skip；`until-fail:10 test_rcrd_process` exit 0，
  每轮 ~11.96 s。isolated_netns=`permission_denied`；host `vcan0` 在授权停旧服务后使用。
- Hypothesis：更早那次 `threads_now == threads_before` 失败原因仍未由本批次区分。
- Conclusion：本批次未复现；验证的是同进程 assemble/teardown，不是物理 CAN / 长跑。不改代码。

### 13.2 epoll / eventfd / signalfd / shutdown

- Fact：duration 路径中 SocketCAN/eventfd/signalfd 注册进同一 epoll；周期线程
  `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`，无 timerfd；duration 通过 eventfd 唤醒
  I/O，`io_reason=EVENTFD`，exit 0。无 duration 的 live `/proc` 为三线程
 （futex / hrtimer_nanosleep / ep_poll），epoll interest 仅为 signalfd/eventfd/CAN。
  SIGTERM 经 signalfd 进入 I/O epoll，ssi_signo=15，先 DEL/close SocketCAN，再 join 并关闭
  剩余 fd，stdout `io_reason=SIGNAL`，exit 0。`strace -p` attach 因 `yama.ptrace_scope=1` 为
  `permission_denied`；syscall 顺序由 strace-as-parent 取得。
- Hypothesis：agent 启动器继承的额外 Cursor log/inotify fd 不在 Runtime epoll；不是 Runtime
  泄漏证据。
- Conclusion：与 `RUNTIME_PROCESS_THREAD_MODEL.md` / `FD_EVENT_MODEL.md` 一致，模型未推翻。
  strace 不做 latency 结论。

### 13.3 Ordinary Linux CPU pressure

- Fact：`rcr_benchmark --help` 打印 usage 并 exit 1（CLI 观察）。`--duration-ms 5000
  --period-us 1000 --fifo-priority 0` 下 5+5 次均 exit 0，`fifo_enabled=0`。baseline P50
  ~54–56 µs，misses 0–4；`stress-ng --cpu 16` 下 misses 升高（33–164），max 可达 ~34 ms。
  governor=powersave。
- Hypothesis：尾部变差来自 ordinary CFS 竞争与桌面负载，不是本批实验要证明的代码缺陷。
- Conclusion：只描述当前普通 Linux、本内核、本负载样本。不声称 PREEMPT_RT 或硬实时。

## 14. 当前禁止新增与首阶段停止线

不新增 EtherCAT、ROS 2、PREEMPT_RT、新 UI、新总线、通用 Transport/plugin、监控平台、Platform
integration、统一大 Reactor 或 Python 分析框架。也不因 node simulator 使用 timerfd 而重写
scheduler。

第一阶段只允许：

1. 固化审计、进程/线程和 FD/event 模型，修正文档路由（已落地）；
2. 执行上述三类实验（本批次已写入 `evidence/head_reality_audit/20260818T033609Z/`）；
3. 只有实验确认真实泄漏、退出超时或竞争后，才比较备选并提交最小代码修复（本批次未确认，不改 C++）；
4. 实验结束即停，不自动实施 Modbus agent systemd、Platform integration 或新 Gate。
   其后用户明确选择 [Post-Audit Local Development SPEC](plans/POST_AUDIT_LOCAL_DEVELOPMENT_SPEC.md)
   为 Current Gate；该 SPEC 后来完成 LD8 并关闭。那是另一次授权，不是本审计自动启动。

Freeze 仍为 `OPEN / DEFERRED`，仍只能由既定物理缺项关闭，软件工作不能替代第 11 项 RS-485
掉线瞬间证据。本文不选择下一 Gate；当前状态只读 [plans/README.md](plans/README.md)。

## Interview Checkpoint

1. 为什么默认 Runtime 是三线程，而 watchdog 不是第四个线程？
2. epoll 报 readable 后为什么 read 仍必须处理 `EAGAIN`？
3. level-triggered 下单次 budget 用尽会怎样？
4. eventfd 与 signalfd 分别解决什么唤醒？
5. 为什么 Runtime scheduler 不使用 timerfd？
6. SIGTERM 到 SocketCAN close 的完整顺序是什么？
7. Runtime 重启后哪些状态必须丢弃？
8. 为什么 current CTest 与旧 dirty physical evidence 不能合并成 clean acceptance？
