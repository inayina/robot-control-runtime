# Orange Pi Real-time Linux 学习与 PREEMPT_RT 对照计划

状态：Learning record / RT0–RT4、RT6、**RT7** 已关闭；**RT4 = Blocked**；**RT5 未开始**。
本文不发布当前任务；当前执行状态只读
[作品集 V1 发布 Gate](plans/PORTFOLIO_V1_RELEASE_PLAN.md)。
冻结日期：2026-08-05
主平台：Orange Pi 4 Pro 4GB
约束：不新增硬件；先用当前普通内核建立基线；内核变更必须可回退
证据 schema：[REALTIME_EVIDENCE_SCHEMA.md](REALTIME_EVIDENCE_SCHEMA.md)
Gate 报告：[PREEMPT_RT_FEASIBILITY_GATE.md](PREEMPT_RT_FEASIBILITY_GATE.md)
Lab 收口：[../evidence/portfolio/orangepi_rt7_wrapup_20260805.md](../evidence/portfolio/orangepi_rt7_wrapup_20260805.md)

## 1. 目标与定位

本计划把 Orange Pi 变成独立的 Real-time Linux 学习平台，回答四个工程问题：

1. 普通 Linux 的周期线程在空载、压力、DVFS 和不同 CPU 核上怎样失约；
2. `SCHED_FIFO`、CPU affinity、内存锁定和有界周期路径分别解决什么问题；
3. 尾延迟来自用户态执行、调度等待、IRQ/内核噪声还是资源竞争；
4. 在同一硬件、同一程序和同一负载下，PREEMPT_RT 是否带来可重复的收益。

这里的“学习硬实时”包括调度理论、Linux 实时机制、最坏情况思维、实验设计和失败归因，
不等于通过一次 benchmark 宣布系统已经具备硬实时保证。

本计划与作品集 V1 并行：

```text
robot-control-runtime V1               Real-time Linux Lab
Runtime / vcan / systemd               Orange Pi 普通内核基线
clean-commit 发布证据                  cyclictest / timerlat / osnoise
          │                                      │
          └──────── 共用 Scheduler 与证据格式 ──┘
                                                 │
                                      PREEMPT_RT 可行性 Gate
                                                 │
                                      同条件内核对照实验
```

- V1 不依赖 PREEMPT_RT，不因内核实验推迟发布；
- Real-time Linux Lab 可以立即开始，不等待 CAN、EtherCAT、ROS 2 或新硬件；
- 实验默认不改变 Runtime Core；只有得到可重复收益且职责匹配时才评审主线改动。

## 2. 术语与证据边界

| 术语 | 本计划中的含义 | 不能据此声称 |
|---|---|---|
| 周期唤醒延迟 | 计划唤醒时刻到线程实际开始运行的差值 | CAN/控制端到端延迟 |
| deadline miss | 当前测量合同下没有在截止时间内完成或唤醒 | 已发生真实机器人危险 |
| `SCHED_FIFO` | POSIX 固定优先级实时调度策略 | 内核已经是 PREEMPT_RT |
| PREEMPT_RT | 提高 Linux 内核可抢占性并线程化更多 IRQ 路径 | RTOS、功能安全或硬实时认证 |
| 最坏样本 `max` | 本次有限测试观察到的最大值 | 所有未来运行的确定上界 |
| 无 miss | 当前样本窗口内没有观察到失约 | 已证明硬实时 |

学习时必须同时掌握测量之外的模型：周期 `T`、截止期 `D`、执行时间 `C`、阻塞时间 `B`、
释放抖动 `J` 和响应时间 `R`。本仓可以用测得的执行时间练习固定优先级响应时间分析，
但测量最大值不能直接当成经过证明的 WCET（Worst-Case Execution Time，最坏执行时间）。

## 3. 平台与工具选择

### 3.1 硬件角色

| 设备 | 角色 | 当前选择 |
|---|---|---|
| Orange Pi 4 Pro 4GB | 主实验平台：ARM、大小核、DVFS、IRQ、systemd、普通内核与候选 RT 内核 | 必用；不新增板卡 |
| ThinkPad | 构建/审查、结果分析、必要时提供 PREEMPT_RT 方法对照 | 辅助；不替代板上结论 |
| CAN/RS-485/EtherCAT 硬件 | 不属于本计划的调度学习依赖 | 不采购 |

Orange Pi 已观察到 6×Cortex-A55 与 2×Cortex-A76，但脚本不得硬编码 CPU 编号。每次运行先
根据 `lscpu`、cpufreq 与 sysfs 记录实际拓扑，再选择一个 A55 和一个 A76 核。

### 3.2 软件与测量职责

| 工具 | 回答的问题 | 使用边界 |
|---|---|---|
| `rcr_benchmark` | 本仓绝对时间睡眠与周期线程表现怎样 | 当前只测空/受控 callback，不替代端到端测量 |
| `cyclictest` | 标准周期唤醒基线怎样 | 独立运行；不与其他诊断器同时争用同一 RT 优先级 |
| `stress-ng` | CPU、内存和 I/O 压力如何放大尾延迟 | 缺失时记 `unsupported`，不能降级成 idle 后写 PASS |
| `timerlat` / `osnoise` | IRQ、线程调度和内核噪声从哪里来 | 诊断运行单独保存，工具本身会扰动系统 |
| `perf sched` | 线程被谁阻塞或抢占 | 用于解释异常，不作为低扰动基准 |

当前 `6.6.98-sun60iw2` 只作为普通 BSP 内核基线（`CONFIG_PREEMPT=y`，非 PREEMPT_RT）。
RT4 已判定 **Blocked**：源码↔uImage 未闭环、双启动项不存在、RT 补丁未验证。在重开
Gate 为 Pass 前不得装核；ThinkPad 仅允许方法 Fallback，不得冒充板上 RT 收益。

## 4. 实验合同

### 4.1 固定变量

比较两个结果时，除正在研究的单一变量外，以下字段必须相同：

- Git commit 和 `git_dirty=false`；
- Release 构建、编译器和构建选项；
- 板卡、启动介质、内核配置 hash；
- CPU 核及其类型、affinity、governor；
- 调度策略、优先级、周期和 callback；
- 压力类型、压力进程 affinity、持续时间和重复次数；
- 温度记录方式与采样工具。

任何字段不一致时只能写成两次独立观察，不能计算“提升百分比”。

### 4.2 指标

主指标：

- cycles、deadline misses；
- lateness p50 / p95 / p99 / p99.9 / max；
- callback execution p99 / max（RT3 后加入）；
- IRQ latency 与 thread latency（工具支持时）；
- voluntary / involuntary context switches；
- minor / major page faults；
- 起止温度、起止频率和是否发生降频。

辅助观察：CPU 利用率、内存占用、IRQ 分布和调度事件。辅助工具不能与正式低扰动矩阵
同时开启；定位异常时另建诊断 run。

### 4.3 证据目录合同

后续实现统一写入以下结构；本计划阶段不创建空目录或假报告：

```text
evidence/realtime_linux/<run_id>/
├── environment.txt
├── command.txt
├── samples.txt
├── summary.txt
├── stderr.txt
└── SHA256SUMS
```

`environment.txt` 至少包含：UTC 时间、平台、内核、内核配置 hash、编译器、构建类型、
commit/dirty、CPU 拓扑、affinity、governor、调度策略/实际生效状态、优先级、负载、周期、
时长、工具版本、温度和 `unsupported_reason`。报告必须先写临时目录，完整后再原子发布。

## 5. 分阶段工作包

### RT0：重分类现有样本并冻结问题 — **已关闭（2026-08-05）**

目标：把 2026-08-05 的 5 秒 Debug、CPU0、空 callback 12 格结果保留为 pilot，不继续把它
扩写成正式实时结论。

任务：

1. ~~记录当前样本只能证明测量链路跑通；~~
2. ~~为 `rcr_benchmark`、`cyclictest` 和诊断工具分别冻结输入输出；~~
3. ~~冻结本文件的字段、命名和 `pass/failed/unsupported/not_run` 分类
   （仓库机器可读另保留 `permission_denied`，与 P2 一致）；~~
4. ~~写出第一版任务模型：周期监督线程、I/O 线程、诊断线程的 `T/D/C/B/J`。~~

交付物：[`REALTIME_EVIDENCE_SCHEMA.md`](REALTIME_EVIDENCE_SCHEMA.md)；
pilot 摘要标注见 [`evidence/portfolio/orangepi_scheduler_20260805.txt`](../evidence/portfolio/orangepi_scheduler_20260805.txt)；
目录合同见 [`evidence/realtime_linux/README.md`](../evidence/realtime_linux/README.md)。

退出条件：能够解释 pilot 数字测量了什么、没有测量什么，以及正式矩阵为什么不同 —
**已满足**（见 schema §1 与 §6）。

### RT1：Orange Pi 普通内核 clean Release 基线 — **进行中（runner 已落地）**

目标：在不更换内核、不修改 Runtime 的条件下得到可复现基线。

执行顺序：

1. checkout 同一 clean commit，原生 Release 构建并跑非硬件测试；
2. 记录 CPU 拓扑，选择一个 A76 主测核和一个 A55 对照核；
3. 先跑 60 秒 smoke，确认 affinity/FIFO/负载确实生效；
4. 正式单格 30 分钟，每个代表格重复 3 次；
5. 单格失败后保留原报告，不自动换条件重跑成绿色结果。

最小矩阵控制在 10 格，不做无边界组合爆炸：

| 组 | CPU | governor | policy | workload | 目的 |
|---|---|---|---|---|---|
| 1–6 | A76 | performance | OTHER/FIFO | idle、其他核 CPU 压力、同核 CPU 压力 | 主矩阵 |
| 7–8 | A55 | performance | OTHER/FIFO | 其他核 CPU 压力 | 大小核对照 |
| 9–10 | A76 | ondemand | OTHER/FIFO | 其他核 CPU 压力 | DVFS 对照 |

主周期为 1 ms；5 ms 只在 `OTHER+stress` 与 `FIFO+stress` 两个代表格交叉检查。FIFO 初始
优先级沿用 10，不直接使用 99。压力进程与被测线程的 CPU 放置必须分别记录。

入口：

- 共享实现：`linux/scripts/run_realtime_linux_rt1.sh` + `lib/cpu_topology.sh`
- 板上 smoke：`sudo bash deploy/orangepi/rt1_smoke_once.sh`
- 板上 formal：`sudo bash deploy/orangepi/rt1_formal_once.sh`（须先审阅 smoke）

**当前状态**：60s smoke 已跑完并审阅通过（`20260805T103150Z_orangepi_rt1_smoke`，
10/10 pass，governor enforced；`git_dirty=true` 故仍是 smoke 而非 baseline）。
摘要见 [`evidence/portfolio/orangepi_rt1_smoke_20260805.md`](../evidence/portfolio/orangepi_rt1_smoke_20260805.md)。
下一步：提交后板上 clean Release，再跑 30 分钟 formal。

退出条件：10 格均有完整报告；unsupported 与 failed 可存在，但不得缺报告；三次重复能够
解释离群值，且所有公开结论明确为普通内核的周期唤醒基线。

### RT2：标准工具基线与异常归因 — **已关闭（2026-08-05，experiment）**

目标：区分“本仓 benchmark 行为”与“内核本身的调度噪声”。

任务：

1. ~~用 `cyclictest` 复现 RT1 的四个代表条件；~~
2. ~~从 RT1/`cyclictest` 选择 max 或 p99.9 异常明显的条件；~~
3. ~~单独运行 `timerlat`/`osnoise`……~~ → **unsupported**（包/内核 tracing 不可用）
4. ~~必要时使用 `perf sched`……~~ → **unsupported**（`perf` 与 `6.6.98-sun60iw2` 不匹配）
5. ~~判断异常属于……~~ → 同核 CFS/`SCHED_OTHER` 竞争（见下）

交付物：

- runner：`linux/scripts/run_realtime_linux_rt2.sh`、`deploy/orangepi/rt2_cyclictest_once.sh`
- 证据：`evidence/realtime_linux/20260805T111247Z_orangepi_rt2_cyclictest/`
- 归因摘要：[`evidence/portfolio/orangepi_rt2_cyclictest_20260805.md`](../evidence/portfolio/orangepi_rt2_cyclictest_20260805.md)

退出条件：至少解释一个尾延迟尖峰 — **已满足**（A76 + OTHER + 同核 stress）；
缺失 IRQ 分解工具已显式记录，不展示“更好看”的单次结果冒充完整归因。

**边界**：`git_dirty=true`，分类为 `experiment`，不是 clean baseline。

### RT3：用户态实时编程实验 — **已关闭（2026-08-05，Orange Pi）**

目标：通过可撤销、一次一个变量的实验学习应用如何减少不确定性。

主平台：**Orange Pi**。板上 `20260805T113406Z_rt3_userspace`（sudo）**5/5 pass**：

1. ~~内存 mlock~~ → 冷触碰 4097 minflt；锁定后再触碰 0
2. ~~锁/PI~~ → PI≈37 ms；无 PI≈78 ms（同核 FIFO）
3. ~~周期路径~~ → prealloc 更短；3 ms 忙等 200/200 miss

摘要：[`evidence/portfolio/orangepi_rt3_userspace_20260805.md`](../evidence/portfolio/orangepi_rt3_userspace_20260805.md)。  
ThinkPad 仅对照。未并入 Runtime；不得声称硬实时。

下一包：clean 提交；可选 RT1 formal。RT4 已关闭为 Blocked（见下）。

### RT4：PREEMPT_RT 可行性 Gate — **已关闭（2026-08-05）：Blocked + Fallback**

目标：先证明“能安全、可重复地构建和回退”，再触碰板端启动内核。

**判定**：**Blocked**（板上禁止安装/覆盖 RT 内核）+ **Fallback**（ThinkPad 方法对照允许）。  
报告：[`docs/PREEMPT_RT_FEASIBILITY_GATE.md`](PREEMPT_RT_FEASIBILITY_GATE.md)  
摘要：[`evidence/portfolio/orangepi_rt4_gate_20260805.md`](../evidence/portfolio/orangepi_rt4_gate_20260805.md)

已查清并足以支撑 Blocked：

- 运行核 `6.6.98-sun60iw2` / 包 `1.0.8`：`CONFIG_PREEMPT=y`，**无** `CONFIG_PREEMPT_RT`，
  无 `/sys/kernel/realtime`；`uname` 中 `PREEMPT` ≠ RT 内核；
- 源码线索指向 `orange-pi-6.6-sun60iw2` / `orangepi-build`，但**未**与本机 `uImage` sha256
  形成复现闭环；离线 RT 构建未做；
- `boot.cmd` 固定加载唯一 `uImage`+`uInitrd`；`/` 与 `/boot` 同为 `mmcblk1p1`；无第二启动项；
- UART/`ttyS0` 与 SSH 物理存在，但“整卡重刷”≠ Gate 要求的并存回退；
- sun60iw2 + PREEMPT_RT 补丁兼容性：**未知**。

Gate 标准（仍适用，供重开）：

- **Pass**：源码/配置可追溯，离线构建成功，启动项并存，回退步骤已静态核对；
- **Blocked**：没有可靠源码、补丁不兼容或没有可验证恢复路径；
- **Fallback**：先在 ThinkPad 完成方法对照，Orange Pi 保留普通内核结果，不能冒充板上 RT。

未通过 Gate：**禁止**覆盖当前内核、唯一启动项或唯一可恢复系统；**禁止**进入 RT5 并宣称
板上 RT 收益；不得使用来源不明“RT”镜像。

### RT5：Orange Pi PREEMPT_RT 同条件对照（**未开始；依赖 RT4 Pass**）

目标：只改变内核变量，回答 PREEMPT_RT 的收益、代价和残余异常。

顺序：

1. 通过独立启动项启动 RT 内核并确认实际配置；
2. 先验证 SSH、存储、网络、时钟、温度、CPU 拓扑和 governor；
3. 跑短 smoke，发生锁死、驱动异常或温控异常立即回退；
4. 复用 RT1 的 10 格矩阵和 RT2 四个标准工具代表格；
5. 普通内核与 RT 内核报告分目录保存，只在匹配字段上比较；
6. 比较 p99.9/max/miss、IRQ/thread latency、吞吐和系统可维护性。

退出条件：能够回答“改善来自哪里、哪些条件没有改善、代价是什么、是否值得用于本项目”。
即使所有样本无 miss，结论仍是受控条件下的 PREEMPT_RT 对照，不是硬实时证明。

### RT6：Runtime 分段时延 — **已关闭（2026-08-05，Orange Pi）**

目标：把单一 wakeup lateness 扩展为可解释的软件调用链，而不是先增加物理总线。

主平台：**Orange Pi**。夹具：[`experiments/realtime_segmented/`](../experiments/realtime_segmented/)
（`rcr_rt6_segments`：`PeriodicScheduler` + SPSC + `eventfd`/`epoll` 软件 peer；**不**改 Core）。

板上 `20260805T115347Z_rt6_segmented`：**4/4 pass**。段：`wakeup` / `callback` / `queue` /
`io_ack` / `e2e`。`cb_busy` 抬高 callback≈500 µs；`io_busy` 抬高 io_ack≈500 µs；drops=0。

摘要：[`evidence/portfolio/orangepi_rt6_segmented_20260805.md`](../evidence/portfolio/orangepi_rt6_segmented_20260805.md)。

退出条件已满足：能分别报告唤醒、callback、排队和 I/O 软件路径时延；明确 **不能**用空
callback p99 代替端到端控制延迟；软件 peer ≠ CAN。

### RT7：学习与作品集收口 — **已关闭（2026-08-05）**

交付物见 [`evidence/portfolio/orangepi_rt7_wrapup_20260805.md`](../evidence/portfolio/orangepi_rt7_wrapup_20260805.md)：

- 普通内核 vs PREEMPT_RT：**诚实记录未做**（Gate Blocked，无 RT5）；
- 延迟来源因果图（同核 OTHER、用户态缺页/PI/分配、wakeup≠e2e、IRQ unsupported）；
- 各阶段复跑入口与作品集摘要索引；
- 面试证据等级表（理解过 / 代码使用过 / Orange Pi 实测过）；
- 负面结果与未采用优化（不装核、不并入 Runtime、不编造 IRQ%）。

**改写后的作品集表述**（勿背计划原稿中的“已对比 PREEMPT_RT”）：

> 在 Orange Pi 4 Pro 上建立 Real-time Linux 实验环境：用同一套周期测量合同在普通内核上
> 量化 affinity、DVFS、同核 OTHER 压力与用户态抖动源；PREEMPT_RT 因 Gate Blocked 未安装。
> 保留环境元数据、失败边界与不能声称清单。

知识卡：§5.4 Gate、§5.8 分段、§5.9 WCET/IRQ/收口。

### RT5 提醒

RT5 仍依赖 RT4 Pass；收口**不**解除装核禁止。

## 6. 安全与停止规则

- FIFO 首轮只用优先级 10；不得从 99 开始，也不得让无限循环持有高优先级；
- 所有高优先级实验必须限时，保留第二 SSH 会话和普通优先级恢复入口；
- 不以长期 root 运行 Runtime；权限通过 rlimit/systemd 最小化配置并记录是否生效；
- `performance` governor 只在实验窗口启用，实验结束恢复原配置；
- 正式矩阵与 tracing 分开运行，避免观察工具改变被测分布；
- 出现温度、降频、文件系统、网络或启动异常时停止采样，不能继续拼接报告；
- 没有双启动/可靠回退路径时停止 RT4，不安装 PREEMPT_RT；
- 普通内核与 RT 内核无法保持同条件时停止计算改善比例；
- 不因 RT 实验引入 CAN、EtherCAT、ROS 2、Dashboard 或新硬件。

## 7. 提交与实施边界

后续实施按可审计提交拆分：

1. `docs:` 本计划、术语与证据 schema；
2. `test:` host snapshot 与标准工具 wrapper，不修改 Runtime；
3. `test:` clean Release 普通内核基线；
4. `experiment:` mlock/page-fault、锁/PI 和分段时延，每项独立提交；
5. `docs:` PREEMPT_RT 可行性与恢复方案；
6. `test:` RT 内核同条件报告；
7. `docs:` 知识卡、对照结论与作品集摘要。

内核镜像、原始大样本、主机地址和临时 trace 默认不提交；仓库只提交脚本、脱敏摘要、hash
和必要的小型代表样本。任何 Runtime 代码变更必须重新跑原有 CTest、sanitizer 与功能 Gate。

## 8. 当前下一步

Real-time Linux Lab **文档收口已完成**（RT7）。板上主证据仍为 dirty。下一动作：

1. **提交** RT0–RT7 相关文档与脚本；板上 clean checkout；
2. 可选 RT1 30min formal（普通内核，盯温度）；
3. V1 作品集 clean Release 证据（与 Lab 分开叙述）；
4. **不要**做 RT5，直到 Gate 重开为 Pass。
