# Real-time Linux 证据 Schema（RT0 冻结）

状态：Frozen（RT0）  
冻结日期：2026-08-05  
关联：[Real-time Linux 学习计划](REALTIME_LINUX_LEARNING_PLAN.md)、
[证据 Schema（P2）](EVIDENCE_SCHEMA.md)

本文件冻结 Real-time Linux Lab 的工具输入输出、证据字段、结果分类和第一版任务模型。
它扩展 P2 公共 schema，不替换 ThinkPad/vcan 故障矩阵合同。RT0 **不**采集新样本、
**不**改 Runtime、**不**改 governor/内核。

## 1. Pilot 重分类（2026-08-05 B3）

脱敏摘要：[`evidence/portfolio/orangepi_scheduler_20260805.txt`](../evidence/portfolio/orangepi_scheduler_20260805.txt)  
本地原文（默认不入库）：`evidence/orangepi_baseline/20260805T085844Z/`

| 字段 | Pilot 实际值 | 正式矩阵要求（RT1） |
|---|---|---|
| `classification` | `pilot` | `baseline` / `diagnostic` |
| `git_dirty` | `true` | `false` |
| `build_type` | `Debug` | `Release` |
| `duration_ms` | `5000` | smoke `60000`；正式格 `1800000`（30 min） |
| `cpu_affinity` | `0`（Cortex-A55） | 先记录拓扑，再选一个 A76 主测核 + A55 对照 |
| `governor` | `ondemand`（全核） | 主矩阵 `performance`；另有 ondemand 对照格 |
| `callback` | 空（无 `--callback-delay-us`） | 仍为空 callback；callback 执行时间属 RT3/RT6 |
| `repeat` | 单次 | 代表格重复 3 次 |
| 矩阵形状 | P2 的 12 格（OTHER/FIFO × idle/stress × 1/5/10 ms） | RT1 的 10 格（见学习计划 §5） |

### 1.1 Pilot 数字测量了什么

- 本仓 `PeriodicScheduler` + `rcr_benchmark` 在 Orange Pi 普通内核上能跑通；
- `environment.txt` / 分格 `summary.txt` / lateness 样本的写入链路可用；
- 在 **5 秒、Debug、CPU0/A55、空 callback、dirty tree** 条件下，OTHER vs FIFO、
  idle vs 同核 `stress-ng` 的唤醒 lateness 有可观察差异（例如 FIFO+stress 相对
  OTHER+stress 减少 miss）；
- `affinity_enabled` / `fifo_enabled` 可被脚本读回，权限失败可与代码失败区分。

### 1.2 Pilot 数字没有测量什么

- clean commit / Release 构建下的可复现基线；
- A76 主测核、30 分钟尾延迟、三次重复离群值；
- callback 执行时间、I/O 排队、CAN/控制端到端延迟；
- IRQ latency vs thread latency（需 `timerlat`/`osnoise` 等诊断工具）；
- PREEMPT_RT、硬实时上界、功能安全或“板上已达实时”。

**合同**：Pilot 只证明测量链路与临时调度差异可见；不得扩写成正式实时结论，也不得与
RT1 报告计算“提升百分比”。

## 2. 结果枚举（与 P2 对齐，不可混用）

| 值 | 含义 | Real-time Lab 典型场景 |
|---|---|---|
| `pass` | 命令成功且该工具合同下的断言满足 | 矩阵格跑完且 FIFO/affinity 按请求生效（若请求了） |
| `failed` | 命令或断言失败（代码/配置/工具异常退出） | stress 启动失败、样本溢出、非权限类 start 失败 |
| `permission_denied` | 缺权限，不是逻辑 PASS | `SCHED_FIFO` / affinity / raw 诊断能力不足 |
| `unsupported` | 环境缺工具或机制 | 无 `stress-ng`、无 `cyclictest`、无 `timerlat` |
| `not_run` | 清单有、本轮未执行 | RT2 诊断项尚未开跑 |

学习计划正文写的 `pass/failed/unsupported/not_run` 是对外叙述子集；仓库机器可读字段
**必须**保留 `permission_denied`，与 [`EVIDENCE_SCHEMA.md`](EVIDENCE_SCHEMA.md) §1 一致。
缺元数据必须报错，不得猜测填充。

## 3. 工具输入输出合同

### 3.1 `rcr_benchmark`（本仓主测）

**回答的问题**：本仓绝对时间睡眠周期线程的唤醒 lateness / miss 怎样。

| 方向 | 冻结内容 |
|---|---|
| 输入 | `--duration-ms`、`--period-us`、可选 `--callback-delay-us`（默认 0）、`--fifo-priority`、`--require-fifo`、`--cpu-affinity`、`--samples-out` / `--no-samples` |
| 输出（stdout key=value） | `duration_ms`、`period_us`、`callback_delay_us`、`fifo_*`、`affinity_*`、`worker_error`、`cycles`、`deadline_misses`、`lateness_{min,mean,max}_ns`、启用采样时的 `lateness_p{50,95,99,99_9}_ns`、`percentile_algorithm`、`sample_*` |
| 原始样本 | `samples_lateness_ns.txt`：每行一个 `int64` 纳秒，语义为 wakeup lateness |
| 成功退出 | `0`；`--require-fifo` 失败或样本溢出为非零 |
| 不测 | CAN/控制端到端、IRQ 分解、硬实时证明 |

矩阵 runner 仍可把“请求 FIFO 但 `fifo_enabled=0`”解释为 `permission_denied`，即使工具
退出码为 0——这是脚本层合同，须写入该格 `summary.txt` 的 `result=`。

### 3.2 `cyclictest`（标准周期对照）

**回答的问题**：标准工具看到的周期唤醒基线怎样；用来对照“本仓行为 vs 内核噪声”。

| 方向 | 冻结内容 |
|---|---|
| 输入（最小记录） | 周期、时长、CPU affinity、策略/优先级、histogram/间隔选项、完整命令行 |
| 输出 | 工具原生报告 + 本仓包装的 `environment.txt` / `summary.txt`（含 `tool=cyclictest`、`tool_version`、`result=`） |
| 运行规则 | **独立** run；不与 `timerlat`/`osnoise`/`perf sched` 或另一高优先级诊断同时争用同一 RT 优先级 |
| 缺失 | 记 `unsupported`，不得把缺工具写成 idle PASS |

RT0 只冻结字段与职责；包装脚本属于 RT1/RT2 实施，本阶段不新增空壳脚本冒充已测。

### 3.3 诊断工具（`timerlat` / `osnoise` / `perf sched`）

**回答的问题**：异常尾延迟更像 IRQ、线程调度噪声，还是可命名的阻塞/抢占者。

| 方向 | 冻结内容 |
|---|---|
| 输入 | 与被解释的 baseline 格匹配的 CPU/时长；一次只回答一个假设 |
| 输出 | `evidence/realtime_linux/<run_id>/` 下独立目录；`summary.txt` 含 `tool=`、`hypothesis=`、`result=` |
| 分类 | 正式低扰动矩阵格不得与 tracing 同开；诊断 run 的 `classification=diagnostic` |
| 扰动 | 工具本身会改变被测分布；结论只能解释“该诊断窗口”，不能回写覆盖 baseline 数字 |

### 3.4 `stress-ng`（负载）

| 方向 | 冻结内容 |
|---|---|
| 输入 | CPU 工作者数、timeout、`--taskset`（压力进程放置必须单独记录） |
| 输出 | 压力命令写入格内 `stress_command=`；日志可放 `stress.log` |
| 缺失 | `result=unsupported`，**禁止**降级成 idle 后写 PASS |

## 4. 证据目录与命名

```text
evidence/realtime_linux/<run_id>/
├── environment.txt
├── command.txt
├── topology.txt             # RT1 矩阵
├── summary.txt
├── stderr.txt
├── SHA256SUMS
└── cells/<cell_id>/         # RT1 矩阵：每格独立样本与 summary
    ├── summary.txt
    ├── samples.txt
    ├── bench_stdout.txt
    ├── bench_stderr.txt
    └── stress.log           # 仅压力格
```

单工具诊断 run 仍可将样本放在顶层 `samples.txt`。  
`run_id` 示例：`20260805T120000Z_orangepi_rt1_smoke`。  
Pilot 历史路径 `evidence/orangepi_baseline/<stamp>/` **保留**，不再改写原始文件；仅在
摘要与索引中标注 `classification=pilot`。

报告必须先写临时目录，校验完整后再原子发布到最终路径；目标已存在则拒绝覆盖。
RT1 入口：`linux/scripts/run_realtime_linux_rt1.sh`；板上
`deploy/orangepi/rt1_smoke_once.sh` / `rt1_formal_once.sh`。

### 4.1 `environment.txt` 必填字段

在 P2 公共字段之上，Real-time Lab 还要求：

| 字段 | 说明 |
|---|---|
| `classification` | `pilot` / `smoke` / `baseline` / `diagnostic` / `experiment` |
| `platform` | `orangepi` / `thinkpad` |
| `kernel_config_hash` | 配置可追溯 hash；不可得则 `unavailable` 并写原因 |
| `tool` | `rcr_benchmark` / `cyclictest` / `timerlat` / `osnoise` / `perf_sched` / … |
| `tool_version` | 版本字符串；不可得则 `unavailable` |
| `cpu_topology_summary` | 核数与 big.LITTLE 摘要 |
| `cpu_affinity` | 被测线程绑定；未绑定 `-1` |
| `cpu_class` | 如 `Cortex-A76` / `Cortex-A55` / `unknown` |
| `stress_affinity` | 压力进程放置；无压力写 `none` |
| `policy_requested` / `policy_effective` | 请求与实际生效策略 |
| `fifo_priority` | 请求优先级；未用 FIFO 写 `0` |
| `governor` | 实验窗口实际 governor |
| `period_us` / `duration_ms` | 周期与时长 |
| `callback_delay_us` | 默认 `0` |
| `load` | `idle` / `stress-ng` / … |
| `temp_c_start` / `temp_c_end` | 起止温度；不可读 `unavailable` |
| `freq_start_khz` / `freq_end_khz` | 起止频率；用于判断降频 |
| `unsupported_reason` | 无则空或 `none` |
| `notes` | 自由文本；须声明“非硬实时”边界 |

## 5. 第一版任务模型（T / D / C / B / J）

学习用符号（不是已证明的 WCET）：

| 符号 | 中文 | 英文 | 本仓直觉 |
|---|---|---|---|
| `T` | 周期 | Period | 两次计划释放之间的间隔 |
| `D` | 截止期 | Deadline | 本周期内必须完成的相对时间界限；当前常取 `D = T` |
| `C` | 执行时间 | Execution time | 一次释放后实际占用 CPU 的时间 |
| `B` | 阻塞时间 | Blocking time | 被低优先级临界区、同核竞争、缺页等拖住的时间 |
| `J` | 释放抖动 | Release jitter | 计划释放与实际变为可运行之间的偏差来源之一 |
| `R` | 响应时间 | Response time | 从释放到完成；固定优先级分析练习用，测量 max ≠ 证明上界 |

### 5.1 周期监督线程（`PeriodicScheduler` / `rcr_benchmark` worker）

| 量 | 当前合同 |
|---|---|
| `T` | `--period-us`（RT1 主测 1000 µs） |
| `D` | 与 period 对齐的绝对边界；越过则记 `deadline_misses` 并跳过旧边界 |
| `C` | 空 callback 时接近采样与记账开销；`--callback-delay-us` 只用于受控过载实验 |
| `B` | 同核 stress、内核不可抢占段、锁、缺页等；RT0 不分解 |
| `J` | 主要被 `wakeup_lateness_ns` 观察；不是 CAN 往返 |

优先级初值：FIFO **10**（不是 99）。关闭顺序：`request_stop` → 当前边界 → `join`。

### 5.2 I/O 线程（`CanIoLoop` / 将来软件 peer）

| 量 | 当前合同 |
|---|---|
| `T` | 事件驱动，**不是**固定周期任务；无名义 `T` |
| `D` | 由协议/会话截止时间与有界队列表达，不是 scheduler period |
| `C` | `epoll_wait` 回调内的编解码与投递；必须有界 |
| `B` | socket/缓冲区、mutex、对端无响应；不得在周期线程里做无界阻塞 I/O |
| `J` | 就绪到处理的排队延迟；属 RT6 分段时延，不在 pilot 空 callback 数字里 |

Orange Pi 当前无 `CONFIG_CAN` 时，I/O 线程不进入 RT1 主矩阵；RT6 可先用进程内/`eventfd`
软件路径练习分段测量。

### 5.3 诊断线程 / 诊断进程

| 量 | 当前合同 |
|---|---|
| `T`/`D` | 由诊断工具自己的采样周期决定；**低于**被测 FIFO 优先级，或完全离线于正式矩阵 |
| `C` | tracing/采样开销本身会扰动系统 |
| `B`/`J` | 用于解释异常，不作为低扰动 baseline 的一部分 |

规则：正式矩阵格与诊断 run 分目录；不得为了更低数字同时打开多个“优化”。

## 6. 正式矩阵为何不同于 Pilot

1. **可复现性**：clean commit + Release，才能跨日对照；
2. **统计窗口**：5 秒看不到 30 分钟尾部；p99.9/max 对短窗口极敏感；
3. **CPU 类**：CPU0/A55 pilot ≠ A76 主测核结论；
4. **DVFS**：ondemand 把频率当成隐藏变量；主矩阵先固定 `performance`；
5. **重复与归因**：单次 12 格通过 ≠ 三次重复可解释离群；IRQ 分解需独立诊断工具；
6. **矩阵形状**：P2 的 12 格是部署对照；RT1 的 10 格是 Real-time Lab 最小学习矩阵。

## 7. 验证 RT0 退出条件

能够口头或书面回答：

1. Pilot 的 p99/max 测量的是什么时钟路径上的什么量？
2. 为什么不能拿 Pilot 对 RT1 算提升百分比？
3. `rcr_benchmark` 与 `cyclictest` 各回答什么问题、何时记 `unsupported`？
4. 周期线程、I/O 线程、诊断线程的 `T/D/C/B/J` 哪些已有代码合同、哪些仍是学习模型？

低风险复核（不产生新正式基线）：

```bash
# 只读核对 pilot 摘要边界
sed -n '1,20p' evidence/portfolio/orangepi_scheduler_20260805.txt
# 本机短跑验证工具仍输出冻结字段（非 Orange Pi 结论）
./build/linux/rcr_benchmark --duration-ms 200 --period-us 1000 --no-samples | head
```
