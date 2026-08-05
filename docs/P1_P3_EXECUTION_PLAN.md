# P1–P3 详细执行计划

状态：P1/P2 实现完成；P3-A0/A1/A2 完成；P3-B **部分关闭**（B0–B3 有板上证据；
无 CONFIG_CAN → `rcrd` 未常驻；B4 未关）
计划日期：2026-08-01（进度注记 2026-08-05）
前置基线：`e4a11fe`（阶段 1 已关闭；正式 vcan 证据测试源码为 `c5fd92f`）
P1/P2 功能已实现；`342fb0d` 上最新本地证据已覆盖 vcan、故障矩阵和 12 格基线，用户决定
不提交这些运行产物。P3-G0 修复了 sanitizer 重跑空报告；P3-A0 冻结了
`/opt/robot-control-runtime` release/current 合同与 dry-run 安装/回滚脚本；P3-A1 落地了
三个 systemd unit 与 `verify_units.sh`；P3-A2 落地了 bring-up 勾选表、共享 12 格
`run_benchmark_matrix.sh` 与主机快照脚本。2026-08-05 起 Orange Pi 4 Pro 上已执行
B0–B3（证据目录 `evidence/orangepi*`）；厂商内核未启用 SocketCAN。任何 Orange Pi
结论都不能用 ThinkPad 结果替代。

本文把长期路线中的阶段 2～4 转成可以逐项编码、测试和审查的工作包：

| 本文名称 | 路线图阶段 | 目标 | 硬件依赖 |
|---|---|---|---|
| P1 | 阶段 2 | 可部署的 `rcrd`、真实 fd 生命周期和有界退出 | 无；ThinkPad + `vcan0` |
| P2 | 阶段 3 | ThinkPad 故障矩阵、sanitizer 和 benchmark 基线 | 无；FIFO/压力测试需主机权限和工具 |
| P3 | 阶段 4 | Orange Pi 4 Pro 部署准备、systemd、权限和 ARM 实测 | 静态准备不需要；实测需要目标板 |

本文的 P1～P3 是阶段 1 关闭后的执行编号；不要与
[上一阶段计划](CURRENT_PHASE_PLAN.md) 中已经完成的 P1～P4 内部工作包混淆。

长期范围仍以 [SPEC](../SPEC.md) 和 [后续开发路线](DEVELOPMENT_ROADMAP.md) 为准；
本文是 P1～P3 的近期执行权威。工作包必须按 Gate 顺序推进，后一个 Gate 不能用文档或
模拟结果替代前一个 Gate 的运行证据。

## 1. 总体边界和共同完成标准

### 1.1 目标调用链

```text
Application / Test
        ↓ publish normal output
LinuxRuntime + PeriodicScheduler
        ↓ latest-wins output              ↑ bounded input/fault events
CAN V1 codec                              NodeSupervisor
        ↓                                 ↑
I/O loop: epoll(SocketCAN, eventfd, signalfd)
        ↓
vcan0 ←→ independent rcr_node_sim
```

- `LinuxRuntime` 负责状态、命令 deadline、watchdog 和 fail-closed，不拥有 socket；
- I/O 路径拥有 Linux fd，只做收发、codec 边界和事件转交，不在 I/O 线程复制状态机；
- 周期线程不等待 socket、不写磁盘、不动态拼接日志；
- 普通输出目标继续使用 latest-wins mailbox；输入边沿、故障、重启和状态迁移进入有界队列，
  队列满时必须锁存可见故障，不能静默覆盖；
- V1 只监督一个明确配置的 CAN 节点，不提前建立多总线 `Transport` 或插件框架；
- 软件 EStop、联锁和 Fault 仍是行为演示，不是功能安全或硬件急停保证。

### 1.2 共同工程交付物

每个涉及代码的工作包完成时必须同时交付：

1. 编码前说明问题、所属层、输入输出、线程/时间模型、资源 owner 和失败行为；
2. 比较至少一个合理备选，记录不选原因；
3. 为并发、时钟、关闭顺序、权限降级和恢复路径补充中文设计注释；
4. 单元或集成测试，且失败场景不依赖人工抢时机；
5. 更新 [知识库](KNOWLEDGE_BASE.md)：直觉解释、内核边界、项目调用链、观察实验、
   30 秒面试回答、追问和尚未证明的边界；
6. 报告实际测试环境、结果、替代方案、取舍和仍未实现的能力。

### 1.3 资源所有权总表

计划中的 owner 先冻结如下；实现证据证明不成立时才调整并记录理由：

| 资源 | 唯一 owner | 非 owning 使用者 | 关闭原则 |
|---|---|---|---|
| `SocketCan` | I/O loop | epoll 仅保存整数 fd | 先从 epoll 删除，再由 RAII 关闭 |
| epoll fd | I/O loop | 无 | I/O 线程结束后关闭 |
| `eventfd` | daemon lifecycle | I/O loop 读取；停止方只写入 | 写停止事件、join I/O，再关闭 |
| `signalfd` | daemon lifecycle | I/O loop 读取 | main 在建线程前 block signal；join 后关闭 |
| scheduler thread | `LinuxRuntime`/`PeriodicScheduler` | daemon 查询 snapshot | request stop、join 后销毁 Runtime |
| I/O thread | daemon lifecycle | main 负责 join | 任何 fd 析构前必须 join |
| 输入/故障队列 | Runtime composition | I/O 生产、周期监督消费 | daemon 停止后统一销毁 |
| trace/evidence 文件 | 非周期诊断路径 | Runtime 只提供快照 | 所有 worker 停止后导出 |

`SocketCan` 继续使用定制 RAII。`eventfd`、`signalfd` 和 epoll fd 若出现重复关闭样板，可用
一个只表达“唯一拥有 Linux fd”的可移动值类型；不使用 `shared_ptr`。组件生命周期固定时
直接作为成员或栈对象，只有真实运行时多态和堆对象唯一所有权出现时才使用 `unique_ptr`。

### 1.4 源码目录规则

公共头文件保持在 `linux/include/rcr/*.hpp`，避免在 API 尚未稳定时批量改变 include 路径。
实现文件按已经存在的职责放置：

```text
linux/src/
├── core/       状态机、调度、watchdog、mailbox、trace、Runtime 和输入事件
├── can/        CAN V1 线协议编解码
├── linux/      SocketCAN、epoll、fd RAII 和 vcan 探测
├── daemon/     rcrd 生命周期与组件组合
└── sim/        独立节点模拟逻辑
```

目录只是依赖方向的导航，不代表每层都要建立基类。`apps/` 继续保存进程入口，`tests/`
暂时保持按测试目标命名；只有测试数量明显妨碍查找时再镜像实现目录。

## 2. P1：可部署 Runtime daemon

### P1-G0：冻结 daemon 合同和可测试边界

**问题与所属层**：现有 Core、codec 和模拟器彼此可运行，但没有 Application composition
root。先冻结进程合同，避免一边编码一边改变线程和退出语义。

**输入**：`LinuxRuntime`、`SocketCan`、`EpollReactor`、CAN V1、`rcr_node_sim`。
**输出**：daemon 状态/退出码、最小参数、线程图、owner 表和测试接口说明。

执行项：

1. 定义 `rcrd` 最小参数：`--can`、`--node-id`、周期、heartbeat timeout、FIFO priority、
   CPU affinity（未配置时不绑定）和可选运行时长；
2. `linux/configs/runtime_v1.yaml` 继续标记为草案；P1 使用明确 CLI 参数，不引入 YAML 库，
   也不手写不完整 YAML parser；
3. 定义退出码：配置错误、fd/接口错误、权限强制失败、worker 失败、正常 SIGTERM；
4. 定义节点在线、重启、通信超时、协议拒绝、队列溢出如何映射到 snapshot/trace/退出策略；
5. 明确测试调用应用服务对象，不为了测试建立 REST、Unix socket 或复杂 `rcrctl`。
6. 独立运行的 `rcrd` 首版只负责生命周期和节点监督，不自动发送演示输出；输出发送链由
   集成测试通过 Application 服务 API 驱动，避免在生产 daemon 中留下测试命令入口。

**备选**：立即引入 YAML + schema。暂不选择，因为只有一个 daemon 配置消费者，会增加依赖、
错误定位和 Orange Pi 部署成本；systemd 的 `ExecStart` 足以承载 V1 少量参数。

**验证/Gate**：每个参数、退出码和故障都有唯一处理方；没有第二个真实实现时不新增
Transport/Backend 抽象。

### P1-W1：Linux fd RAII 与停止唤醒原语

**问题与所属层**：`rcr_node_sim` 已证明 epoll/timerfd/signalfd 可用，但 daemon 还需要跨线程
停止。此包只建立 Linux fd 生命周期，不接业务状态。

**输入/输出**：创建好的 epoll、eventfd、signalfd；输出可移动、不可复制的 owner 和稳定错误。
**线程/时间**：main 在线程创建前 block `SIGINT/SIGTERM`；I/O 线程从 signalfd 读取；任意正常
停止请求向 eventfd 写入一次，无轮询 sleep。
**失败行为**：创建、注册、读写失败返回明确 `Result`；fd 不足时不启动部分 daemon。

执行项：

1. 增加最小 owning-fd 值类型，或证明局部 RAII 已足够；移动后源 fd 必须为 `-1`；
2. 建立 eventfd 非阻塞写/读和计数排空；处理 `EINTR`、`EAGAIN`；
3. 建立 signal mask + signalfd，确保 scheduler/I/O 线程继承已阻塞的 signal mask；
4. 测试打开、移动、重复 stop、注册失败和析构，不通过 `/proc` 猜测 owner；
5. 知识卡解释“fd 是整数句柄但整数不是 owner”、eventfd 与 condition_variable 的边界、
   signalfd 为什么能把信号纳入 epoll。

**备选**：signal handler 设置全局 atomic。暂不选择，因为 handler 可调用函数受限，且不能自然
唤醒阻塞在 epoll 的线程；signalfd 的事件顺序更容易测试。

**Gate**：100 次创建/停止测试无 fd 数量增长；SIGTERM 不执行异步信号不安全代码。

### P1-W2：单节点监督与有界输入/故障队列

**问题与所属层**：heartbeat、NodeStatus 和节点重启不能进入 latest-wins 输出 mailbox，也不能
由 I/O 线程直接并发修改状态机。

**输入**：解码后的 `NodeHeartbeat`、`NodeStatus` 和协议/队列错误。
**输出**：有序 Runtime 输入事件、节点 snapshot、通信 deadline 和 overflow latch。
**线程/时间**：I/O 单生产者，周期监督单消费者；所有 deadline 使用接收端
`CLOCK_MONOTONIC`，不使用发送方或墙钟时间。
**所有权**：队列由 Runtime composition 拥有；事件按值存储，不让指针跨线程借用栈对象。

执行项：

1. 定义具体的 V1 输入事件集合，不制作通用消息总线；
2. 固定容量，在构造时分配；周期路径不扩容；
3. 队列满时递增诊断计数并锁存 Fault/CommLoss，禁止丢掉故障后继续 Active；
4. 监督 boot/session、u16 heartbeat sequence、最后接收时间和 300 ms 默认 timeout；
5. 节点重启产生显式事件，清除旧 session/输出，不自动恢复 Active 或重放命令；
6. 限制每周期最大消费数，避免 CAN 洪泛无限占用周期线程；剩余积压可见；
7. 测试正常、重复/回绕、旧 boot/session、超时、恢复、overflow 和洪泛预算。

**备选**：每种消息一个 atomic “最新值”。暂不选择，因为节点重启、fault 边沿和状态迁移会被
后值覆盖，无法证明处理过关键事件。

**Gate**：所有关键事件要么被处理，要么触发可见 overflow fault；恢复必须显式重新建立会话。

### P1-W3：CAN I/O loop

**问题与所属层**：把真实 SocketCAN fd、codec、eventfd、signalfd 和监督事件连成一个 I/O
线程，保持 Core 不依赖 Linux socket。

**输入**：SocketCAN 帧、停止 fd、signal fd、Runtime 输出 mailbox。
**输出**：解码事件进入有界队列；合法输出编码后发往 CAN；I/O 统计和稳定错误。
**线程/时间**：一个 I/O 线程阻塞在 epoll；每次唤醒有帧/事件预算，不能让持续 CAN 流量饿死
停止事件。
**所有权**：I/O loop 唯一拥有 SocketCan 和 epoll 注册；只向 Runtime 传值。

执行项：

1. 注册 SocketCAN、eventfd、signalfd，并显式处理 `EPOLLERR/HUP`；
2. SocketCAN 使用 non-blocking drain，区分 WouldBlock 与真实 I/O 错误；
3. decode reject 计数和原因可见，但单个外部坏帧不导致未捕获异常；
4. 从 Runtime 取输出前再次检查 Active/session/deadline；send 失败走 fail-closed；
5. 停止顺序：停止接收新应用命令 → eventfd/signalfd 唤醒 → I/O loop 退出 → join →
   Runtime stop/join scheduler → 移除注册并关闭 fd；实现若需不同顺序必须以死锁/数据丢失
   测试说明原因；
6. 单测使用真实 eventfd/socketpair 等 Linux fd；SocketCAN 端到端仍使用 vcan，不伪造内核行为。

**备选**：阻塞 CAN receive 线程 + 独立 signal/stop 线程。暂不选择，因为增加线程、共享状态和
关闭竞态，而 V1 只有一个 CAN fd。

**Gate**：内部 stop 和 SIGTERM 都能唤醒空闲 epoll；节点退出、CAN 错误和 send 失败可见；
不存在 detach 线程。

### P1-W4：`rcrd` composition root 与生命周期

**问题与所属层**：Application 负责把已验证组件组合成进程，并保证部分启动失败时逆序回收。

执行项：

1. 新增 `linux/apps/rcrd.cpp`，只做参数解析、signal mask、对象构造、start/wait/stop 和退出码；
2. 可测试服务类拥有 Runtime、监督器、队列和 I/O thread；`main()` 不承载业务逻辑；
3. 启动顺序固定并为每一步准备回滚；配置/接口错误不得留下 scheduler 线程；
4. 结构化终端日志至少包含启动环境、调度策略是否生效、状态变化、协议拒绝、overflow、
   worker error 和退出原因；周期 callback 不直接输出；
5. 正常退出前清空普通输出路径；进程重启产生新 session；
6. CLI `--help` 和错误信息能直接告诉用户缺少接口、权限还是参数非法。

**备选**：所有对象和循环直接写在 `main()`。暂不选择，因为部分启动回滚、重复 start/stop 和
集成测试无法在不 fork 进程的情况下稳定验证；但服务类保持具体，不建立接口层级。

**Gate**：配置错误、socket 打开失败、scheduler 强制 FIFO 失败、I/O thread 失败均有确定退出码；
重复 start/stop 不泄漏线程/fd。

### P1-W5：daemon 进程验收与 P1 关闭

执行项：

1. 服务级集成验收在测试进程内调用 Application API，并启动独立 `rcr_node_sim`；命令与
   节点响应只经 `vcan0`，不共享模拟器内部状态；
2. 进程级验收另行启动独立 `rcrd` 与 `rcr_node_sim`，覆盖真实 signal、退出码和 fd 回收；
3. 自动覆盖启动在线、heartbeat 超时、节点重启、新 session、旧命令拒绝、非法帧、队列
   overflow、scheduler callback 失败和 SIGTERM；
4. 记录每个场景期望状态、退出码和最大退出时间；时间窗用协议常量推导并留调度裕量；
5. Linux 侧重复启动/停止至少 100 次，对比 `/proc/self/fd` 或子进程 fd 数；
6. 在干净 commit 上生成 `evidence/rcrd_acceptance/` 正式证据，记录 commit + dirty 状态；
7. 更新 README、SPEC、路线图和知识库，将“daemon 已实现”与“尚未 systemd/ARM 实测”分开。

**P1 Gate**：

- 完整 CTest 通过；
- `rcrd` 与模拟器只经 SocketCAN 完成自动场景；
- SIGTERM 和内部 stop 有界退出，无 fd/thread 泄漏；
- 节点离线、队列溢出和 worker 失败均 fail-closed 且可观察；
- 不声称 systemd、ARM 或硬实时已经验证。

## 3. P2：ThinkPad 证据基线

### P2-W0：证据 schema 与运行清单

**问题**：临时终端输出不能跨 commit、机器和调度配置比较。
**输出**：机器可读数据 + 人可读摘要；原始样本与汇总分开。

执行项：

1. 固定字段：日期、hostname、机器/CPU、OS/kernel、compiler/build type、commit/dirty、
   governor、affinity、策略/priority、FIFO 是否实际生效、周期、时长、负载、温度可用性；
2. 记录命令和退出码，unsupported/permission denied/failed/pass 四种结果不可混用；
3. 证据脚本从仓库根目录运行，拒绝覆盖已有结果；
4. 正式基线从干净 commit 采集，临时结果继续由 `.gitignore` 忽略。

**备选**：只保存 Markdown 表格。暂不选择，因为无法重新计算分位数或自动比较平台；Markdown
只作为机器可读结果的摘要。

**Gate**：同一份原始数据可重复生成相同摘要；元数据缺失时报告明确失败而非填猜测值。

### P2-W1：固定 sanitizer 构建

执行项：

1. 增加明确 CMake options/presets 或脚本：ASan+UBSan、TSan 分开构建；
2. sanitizer 构建使用独立 build 目录，不污染普通 benchmark 二进制；
3. ASan/UBSan 覆盖所有单元测试和 rcrd 生命周期场景；
4. TSan 若因环境 `unexpected memory mapping` 无法启动，记录 unsupported，不得写 PASS；
5. LeakSanitizer 因 ptrace/环境限制关闭时必须出现在报告中；
6. sanitizer 发现问题先修复和回归，不通过 suppressions 隐藏未知竞争。

**备选**：只在 CI 开 sanitizer。暂不选择，因为 Orange Pi 前首先需要本机可复现命令，且当前
尚无 CI 基线；后续 CI 复用同一配置。

**Gate**：一条明确命令可重建每类 sanitizer 结果；unsupported 与代码失败可区分。

### P2-W2：自动故障矩阵

执行项：

1. 把 SPEC `12 转成数据驱动场景表，至少覆盖状态机拒绝、deadline、sequence/session、
   heartbeat、重启、非法帧、mailbox 覆盖、队列 overflow、worker exception、SIGTERM；
2. 每个场景记录前置状态、注入动作、预期 trace/snapshot/退出码和恢复动作；
3. Fault Injection 默认关闭，只由 simulator/test 参数启用；
4. 共享资源不得造成并行测试互相干扰；vcan 缺失在正式矩阵中硬失败；
5. 重复运行验证没有依赖测试顺序或人工时机。

**Gate**：单条命令产生逐场景 PASS/FAIL 和环境元数据；任何未执行场景不能计入通过数。

### P2-W3：benchmark 采样与统计

**问题与所属层**：现有 min/mean/max 只能冒烟；需要分位数和 deadline miss，但统计工作不能
侵入周期 callback。

执行项：

1. 测试开始前预分配采样存储；callback 只写固定位置的整数样本，不排序、不写文件；
2. worker 停止后计算 P50/P95/P99/P99.9、min/max/mean、miss 和 worker error；
3. 明确定义 percentile 算法和样本不足行为；原始值单位固定为 ns；
4. 分开报告调度唤醒 lateness 与端到端 CAN 行为，不把空 callback 数据称作控制延迟；
5. 采样本身的开销通过关闭/开启采样的对照实验说明。

**备选**：周期内维护直方图。首版不选，因为边界和量化误差需要额外设计；预分配原始样本在
当前测试时长内更易审查，统计在非周期上下文完成。

**Gate**：固定输入样本的分位数单测通过；报告可追溯到原始样本和明确算法。

### P2-W4：ThinkPad 调度/负载矩阵

矩阵顺序固定，先建立普通 Linux 基线：

| 策略 | 负载 | 周期 |
|---|---|---|
| `SCHED_OTHER` | idle | 1/5/10 ms |
| `SCHED_OTHER` | `stress-ng` | 1/5/10 ms |
| `SCHED_FIFO` | idle | 1/5/10 ms |
| `SCHED_FIFO` | `stress-ng` | 1/5/10 ms |

执行项：

1. 固定每组时长、CPU affinity、governor 和压力命令；记录压力进程 affinity；
2. FIFO 请求与实际策略分别记录；权限不足不能偷偷降级后标作 FIFO；
3. 先短时 smoke，再运行正式时长；温度不可读就记录 unavailable；
4. 不自动修改系统 governor/limits；脚本先探测并打印需要的显式运维操作；
5. 汇总只比较同周期、同负载和同采样方法的数据。

**Gate**：12 个组合均有 pass/fail/unsupported 结果和原始数据；结果措辞只限本机、该内核、
该配置，不声称硬实时。

### P2-W5：P2 审计和基线提交

执行项：

1. 复跑普通 CTest、vcan/rcrd 验收、sanitizer 和故障矩阵；
2. 审查证据 commit/dirty、环境字段、未执行项和失败项；
3. 提交脚本/代码/文档后，在干净源码 commit 上采集正式 ThinkPad 基线；
4. 单独提交选定的正式证据，临时和重复日志不入库；
5. 知识库增加 sanitizer、数据竞争、调度策略、分位数、governor/affinity 面试卡。

**P2 Gate**：一条命令生成自动故障报告；sanitizer 状态诚实可复现；12 组 benchmark 有明确
状态；ThinkPad 数据被标为 Orange Pi 对照而非部署或硬实时结论。

## 4. P3：Orange Pi 准备与 ARM 部署

P3 分为 G0（入口证据）、A（不依赖板卡的部署资产）和 B（必须在板卡上执行）。P3-A
完成不能把阶段 4 标记为完成。所有运行证据可以按用户决定保留在本地而不提交，但文档必须
如实区分“本地看过”和“仓库内可追溯”。

### 4.1 P3 入口证据审计（2026-08-03）

审计对象均来自当前工作区最新时间戳：

| 证据 | 结果 | 边界 |
|---|---|---|
| vcan 双进程 | `342fb0d`、clean、6/6 PASS | 只证明软件 CAN 路径 |
| 自动故障矩阵 | `342fb0d`、clean、19/19 PASS | FIFO 已生效；包含 worker exit 4 与 SIGTERM |
| ThinkPad 矩阵 | 12/12 有结果；FIFO/affinity 全部实际生效 | 5 秒 Debug 空 callback；不是控制延迟或硬实时 |
| ASan+UBSan | 修复后连续重跑均为 18/18 PASS | 最新报告 `git_dirty=true`，只作本地验证，不是正式 clean 基线 |
| TSan | 修复后连续重跑均为 `unsupported` | 探针为 `unexpected memory mapping`，不能写成 PASS |

ThinkPad 数据还显示：`SCHED_OTHER + stress-ng + 1 ms` 在 5 秒内跳过 2949 个计划边界，
而 6 个 FIFO 格均为 0 miss。这里只能说明该 ThinkPad、内核、governor、CPU 0 和本轮负载
下的唤醒行为；它是 Orange Pi 同条件对照，不是 FIFO 或普通 Linux 的普遍结论。

最初重跑曾留下 ASan/TSan 0 字节报告；该失败直接触发 P3-G0。修复后的报告均非空并带
环境字段，但因为工作区已有未提交修改，sanitizer 结论仍只标为本地验证。上述证据全部是
x86_64 ThinkPad 证据，不是 Orange Pi 4 Pro、物理 CAN 或 EtherCAT 结果。

### 4.2 冻结的部署决策

```text
systemd (root) ── rcr-vcan.service ── 创建/验证 vcan0
                         ↓
systemd (User=rcr) ── rcrd.service ── 监督 vcan0 上 node 1
                         ↑
验收时临时启用 ── rcr-node-sim.service（默认不启用、Fault Injection 关闭）
```

- release 安装到 `/opt/robot-control-runtime/releases/<git-short-sha>/`，
  `/opt/robot-control-runtime/current` 只指向一个已验证 release；回滚只切换到上一明确 release；
- `/etc/robot-control-runtime/` 只保存 systemd drop-in 或部署元数据，不引入 YAML 装载；
- 日志只进入 journal；证据由验收脚本在源码工作区或明确输出目录采集，周期线程不写文件；
- `rcrd` 使用系统用户 `rcr`，无登录 shell，不获得 `CAP_NET_ADMIN`；创建 vcan 的 root
  职责只存在于独立 oneshot；
- 基础 unit 先以 `SCHED_OTHER`、不绑定 CPU 启动。观察目标 CPU 后再通过 drop-in 加
  `--cpu-affinity N --fifo-priority 10 --require-fifo` 和 `LimitRTPRIO=10`；优先不用
  `CAP_SYS_NICE`，只有实机证明 rlimit 不足时才重新评审；
- `rcr-node-sim.service` 只用于 V1 冷启动和恢复验收，不作为生产节点，也不默认随
  `rcrd` 启动。物理 CAN 阶段直接停用它，不改变 Runtime Core；
- 不设置 `WatchdogSec=`：当前 `rcrd` 没有 `sd_notify` 心跳。systemd 只按进程退出码和
  `Restart=on-failure` 管理，不制造一个未实现的 watchdog；
- 原生 aarch64 构建是权威路径；不增加 Docker、Ansible、交叉编译超级构建或通用部署框架。
- 目标板是 Orange Pi 4 Pro 4GB。产品资料中的 A733、4GB LPDDR5、千兆网口和 Wi-Fi 6
  只作为清单预期值；B0 必须从实物、设备树和运行系统重新观察。

### P3-G0：修复证据重跑连续性

**问题**：sanitizer 脚本固定使用 `/tmp/rcr_asan_env.txt` 和 `/tmp/rcr_tsan_env.txt`。
第二次运行时 `rcr_write_environment` 拒绝覆盖旧文件，而外层报告已被重定向截断，因此留下
0 字节文件。ASan 的临时 CTest 输出虽然是 18/18 PASS，也不能替代完整正式报告。

执行项：

1. ~~每次运行使用独立 `mktemp -d`，退出时只清理由本次调用创建的目录；~~
2. ~~报告先写同目录临时文件，字段完整后原子 rename，失败不得留下看似有效的空报告；~~
3. ~~连续运行 ASan/UBSan 和 TSan 各两次，证明不会读到上一轮 commit/环境；~~
4. 在当前 clean commit 上重跑；TSan mapping 问题记 `unsupported`；
   （脚本已修复并验证；正式 clean 重跑需提交后执行。当前本地验证 `git_dirty=true`、
   TSan=`unsupported`。）
5. 保存一次当前提交的完整 CTest/rcrd 重复启停结果；原计划 100 次 Gate 不用旧 commit
   的 `repeat_100.txt` 代替。

**备选**：执行前删除固定 `/tmp` 文件。拒绝，因为并发运行会互相覆盖，失败时也无法确认
文件属于哪次调用。报告文件名改为 `秒精度UTC.PID`，避免同秒连续重跑撞名。

**Gate**：重复运行不产生空报告；报告 commit/dirty 与本轮一致；P3 后续复用这套模式。
（sanitizer 脚本 Gate 已本地关闭；项 4/5 的 clean-commit 正式证据待提交后补齐。）

### P3-A0：部署目录与版本合同（到货前）

状态：**本地完成**（ThinkPad 临时 prefix 自测通过；非 Orange Pi 实装证据）

执行项：

1. ~~建立 `deploy/systemd/`、`deploy/orangepi/` 和 `docs/ORANGE_PI_BRINGUP.md`；~~
2. ~~按 4.2 冻结 release/current 路径、服务用户、证据目录和文件 owner/mode；~~
3. ~~release 目录保存 commit、dirty、compiler、build type 和二进制 SHA-256 的 manifest；~~
   ~~P3 不为此给 `rcrd` 注入构建时 Git 状态，也不增加装饰性 `--version`；~~
4. ~~原生 CMake 构建是首个权威路径；交叉编译只作为可选加速，不建立超级构建；~~
5. ~~安装脚本默认 dry-run，校验 release id、绝对目标和目标边界，拒绝覆盖已有 release；~~
6. ~~回滚为切换 `current` 到上一份明确版本并重启服务，不删除源码、证据或未知文件。~~

**备选**：Docker/Ansible。暂不选择，因为单板单服务不值得引入镜像、网络和权限层；它们会
掩盖 systemd、capability 和原生 Linux 部署知识。

**Gate**：路径、用户、权限和回滚在文档中唯一且互不矛盾。
（`docs/ORANGE_PI_BRINGUP.md` 为权威；`deploy/orangepi/PATHS.md` 为短表。）

### P3-A1：systemd unit 静态设计（到货前）

状态：**本地完成**（unit + drop-in 示例 + `verify_units.sh`；ThinkPad
`systemd-analyze verify`=`pass`；本机可 enable。非 Orange Pi 冷启动证据）

执行项：

1. ~~`rcr-vcan.service`：root、`Type=oneshot`、`RemainAfterExit=yes`，只调用已安装的
   `setup_vcan.sh vcan0`；重复启动必须幂等；~~
2. ~~`rcrd.service`：`Type=simple`、`User=rcr`，前台运行；`Requires/After=rcr-vcan.service`；~~
3. ~~基础 `ExecStart` 使用绝对路径和明确的 CAN/node/period/timeout 参数，不装载 YAML；~~
4. ~~stdout/stderr 进入 journal；SIGTERM 对应 P1 有界退出；`TimeoutStopSec=5s`；~~
5. ~~`Restart=on-failure`、`RestartSec=2s`、30 秒内最多 3 次，避免启动风暴；正常 stop
   和退出码 0 不自动重启；~~
6. ~~`rcr-node-sim.service` 单独提供且默认 disabled；参数中不启用 Fault Injection；~~
7. ~~FIFO/affinity 用显式 drop-in，基础 unit 不硬编码目标 CPU；governor 只记录不修改；~~
8. ~~先加不改变功能语义的最小 hardening；每个限制单独验证，不一次堆叠未知选项；~~
9. ~~在 ThinkPad 用 `systemd-analyze verify` 静态验证；不能把静态验证写成 Orange Pi 证据。~~

**备选**：服务长期以 root 运行。拒绝，因为权限范围过大，且无法展示最小权限部署能力。

**Gate**：unit 静态检查通过；权限不足、退出超时和启动风暴的预期行为有文档。
（见 `deploy/systemd/README.md` 与知识库 §6.6。）

### P3-A2：bring-up 与证据模板（到货前）— 已完成

执行项：

1. ~~清单覆盖 Orange Pi 4 Pro 4GB、5V/3A 供电预期、存储、镜像、OS/kernel、设备树、
   架构、编译器、网络、时间同步和温度；预期值与观察值使用不同字段；~~
   （`deploy/orangepi/BRINGUP_CHECKLIST.md`）
2. ~~SSH 只写密钥与普通用户流程，不保存密码/私钥；~~
3. ~~写出原生 configure/build/test/install 命令和 vcan 创建/重启后的检查；~~
4. ~~准备 systemd、journal、权限、governor、affinity、压力 benchmark、重启/断电测试记录表；~~
5. ~~所有未执行项默认 `NOT_RUN`，不能使用预填 PASS。~~
6. ~~复用 P2 环境字段，增加板卡型号、RAM、供电、启动介质、镜像来源、aarch64、CPU
   大小核拓扑、降频/欠压状态、systemd 版本、service/drop-in 内容与 binary SHA-256；~~
   （`collect_orangepi_host_snapshot.sh` + `EVIDENCE_SCHEMA.md` §4.1）
7. ~~把 benchmark 矩阵主体提取成 ThinkPad/Orange Pi 两个真实调用者共享的参数化脚本；
   平台 wrapper 只选择输出目录和平台标签，不复制 12 格循环。~~
   （`run_benchmark_matrix.sh` + `run_{thinkpad,orangepi}_benchmark_matrix.sh`）

**Gate**：到货后能从空系统按清单操作；模板明确区分观察值、命令、结果和解释。
（模板与共享 runner 已落地；勾选表行默认 `NOT_RUN`，不等于板上 B0–B4 已测。）

### P3-B0：硬件清点与主机基线（到货后）

执行项：

1. 记录准确板卡、RAM、供电、存储、OS image、kernel、DTB/设备树 model、architecture
   和 compiler；确认观察对象确为 Orange Pi 4 Pro 4GB，不只相信包装或商品页；
2. 验证 SSH、DNS、时间同步、磁盘空间、温度读取和稳定供电；
3. 记录 A733 的实际 online/allowed CPU、大小核映射、每个频率策略、默认 governor、
   调度策略、rlimit 和 capability，不预设 CPU 编号，也不先修改系统；
4. 将异常供电、降频或存储错误作为环境失败处理，不归因于 Runtime。

**Gate**：设备身份和基础环境可追溯，连续基础运行无明显供电/存储异常。

### P3-B1：板上原生构建与功能验收（到货后）

执行项：

1. checkout 与 ThinkPad 基线相同的源码 commit；若 P3-G0 修复形成新提交，则 ThinkPad
   对照也使用这个明确的新提交，不能混写为相同 commit；验证 `git_dirty=false`；
2. 原生 CMake Debug 构建并运行不依赖 vcan 的测试；
3. 创建 `vcan0` 后运行强制 SocketCAN、节点模拟器和 rcrd 完整验收；
4. 记录 aarch64 特有编译警告、类型宽度和内核行为差异；不得为了通过而屏蔽未知 warning；
5. 保存 ARM 功能证据。

**Gate**：同一源码 commit 在 aarch64 构建；完整 vcan/rcrd 场景通过。

### P3-B2：systemd 安装、权限和生命周期（到货后）

执行项：

1. 创建普通服务用户、安装 binary/unit，并记录文件 owner/mode；
2. daemon 前台运行，由 systemd 管理；验证 start/status/stop/restart 和 journal；
3. 先验证基础 unit 的普通策略，再验证无 `LimitRTPRIO` 时 `--require-fifo` 明确失败，最后
   加 drop-in 验证 FIFO 成功或记录该 OS 的具体失败；
4. 验证 SIGTERM 在 `TimeoutStopSec` 内结束，无 SIGKILL 才能回收的常态；
5. 验证崩溃重启限制，不形成启动风暴；重启后新 session，旧命令不生效；
6. 冷启动后 `vcan0` 与 `rcrd` 按设计运行；验收 simulator 是否启用必须写入证据，不能把
   daemon 在线和节点在线混成一个状态；
7. 用 `readlink`、manifest 和 SHA-256 验证实际运行 release；演练一次切换上一 release
   的回滚，不删除任何 release。

**Gate**：服务不以 root 常驻；权限是否生效可观察；停止、失败和重启行为符合 P1 合同。

### P3-B3：ARM benchmark 与平台对照（到货后）

执行项：

1. 复用 P2 相同采样程序、5 秒 smoke 和正式时长、周期、策略和负载顺序；正式时长在
   第一次 smoke 后冻结，ThinkPad 对照必须用相同时长；
2. 记录 governor、affinity、所选 CPU 属于 A76 还是 A55、温度/降频和实际 FIFO；
3. 完成普通/FIFO × idle/stress × 1/5/10 ms；
4. 与 ThinkPad 只比较同条件指标，解释架构、CPU、内核和散热差异；
5. 先采默认 governor，再决定是否追加 performance governor 实验；两者不混在同一基线；
6. 不因单次 max 较小就声称实时性更强，不安装 PREEMPT_RT 后再补普通内核基线。

**Gate**：ARM 12 组矩阵均有明确状态和原始数据；对照报告没有跨条件误比。

### P3-B4：恢复、重启与最终部署证据（到货后）

执行项：

1. 自动验证节点退出/重连、daemon 崩溃、SIGTERM、服务 restart 和系统 reboot；
2. 首轮只做正常 `systemctl reboot`。非正常拔电不作为 P3 必需 Gate；若额外执行，只能在
   文件已同步、没有升级操作时进行并记录存储风险；
3. reboot 后确认服务、vcan 设置方式、日志和新 session；
4. 检查 journal 中是否存在启动风暴、权限降级、超时 SIGKILL 或旧命令恢复；
5. 在干净 commit 上保存 `evidence/orangepi/`，报告尚未覆盖 physical CAN 和硬实时。

**P3 Gate**：

- Orange Pi 4 Pro 冷启动后 `rcrd` 按既定策略可用；
- systemd stop 有界，常态无需 SIGKILL；
- 服务普通用户 + 最小权限运行，FIFO 实际状态可见；
- ARM 功能、故障和 12 组 benchmark 证据可复现；
- 重启生成新 session，不重放旧命令；
- 结论只覆盖该 Orange Pi 4 Pro、该 OS/kernel/DTB、所选 CPU 和 `vcan` 软件路径。

### 4.3 推荐执行顺序

```text
G0 sanitizer/当前 rcrd 证据连续性
  → A0 路径、用户、manifest、回滚合同
  → A1 三个 systemd unit + 静态验证
  → ~~A2 bring-up/ARM 证据模板 + 通用矩阵 runner~~
  → B0 板卡与 OS 基线
  → B1 aarch64 原生构建 + vcan/rcrd 功能验收
  → B2 SCHED_OTHER systemd 生命周期
  → B2 FIFO/affinity 最小权限 drop-in
  → B3 ARM 12 格与 ThinkPad 同条件对照
  → B4 reboot、恢复、release 回滚与 P3 审计
```

每一步失败都停在当前 Gate：构建问题不通过 systemd 掩盖，systemd 权限问题不通过 root
常驻绕过，benchmark 环境问题不归因于 Runtime，板卡异常不通过调度参数“调好看”。

## 5. 建议提交边界

保持每个提交可构建、可测试；具体文件名可随实现证据调整，但不把整个 P1～P3 压成一个
commit。

### P1

1. `docs: freeze rcrd lifecycle and failure contract`
2. `linux: add owned fd and bounded runtime events`
3. `linux: connect CAN epoll loop to runtime supervision`
4. `app: add rcrd composition root and bounded shutdown`
5. `test: add repeatable rcrd process acceptance`
6. `docs: record rcrd learning notes and P1 evidence`

### P2

1. `build: add reproducible sanitizer configurations`
2. `test: automate runtime fault matrix`
3. `bench: record latency samples and percentiles`
4. `evidence: add ThinkPad scheduling and load baseline`

### P3

1. ~~`deploy: add systemd unit and Orange Pi bring-up guide`~~（A0/A1/A2 资产已落地）
2. `deploy: validate native ARM runtime lifecycle`（B0–B2，到货后）
3. `evidence: record Orange Pi functional and timing baseline`（B3–B4，到货后）

## 6. 明确延后项

P1～P3 不实现以下内容：

- 通用 Transport、插件系统、线程池；
- REST、Dashboard、ROS 2 Adapter；
- EtherCAT、Modbus、physical CAN；
- ESP32/STM32 固件；
- PREEMPT_RT；
- 硬件功能安全或硬实时保证。

只有 P3 Gate 完成后，才按路线图进入 EtherCAT I/O SubDevice Gate。任何新需求若要求上述能力，
先评估是否会阻塞 V1 主线，再决定新增阶段，不能偷偷塞进当前工作包。
