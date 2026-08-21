# Post-Audit Local Development SPEC

状态：**Active**  
当前 milestone：**LD8 — Local Release Candidate Gate（本机验收关闭；等待后置 Gate）**
上一关闭：LD0（baseline `049894d6fc367743b45de0b846ab73214e736a95`）、LD1（confirmed Runtime OS gap = 0）、LD4/LD5/LD6/LD7（source/test acceptance `28bf3eb`）
编写日期：2026-08-18  
激活日期：2026-08-18（用户选择 B：整份 SPEC 为唯一 Current Gate）  
审计基线（Runtime 代码）：`3c3bba419491cd6d833b9c55c42eab8aca9757d9`
Implementation baseline：`049894d6fc367743b45de0b846ab73214e736a95`（2026-08-18 用户提交；docs/evidence 索引，无 C++）
本机证据：[`20260818T033609Z`](../../evidence/head_reality_audit/20260818T033609Z/NOTES.md)
（`LOCAL / VCAN / CURRENT-HEAD / DIRTY`；原始 batch 按 `.gitignore` 仅本地保留，git 只跟踪目录 README）
Deferred Gate：
[Closed-Loop Portfolio Freeze](CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)
（`Deferred / still open`，不得标 CLOSED）

本文是当前唯一 Current Gate。它把 HEAD audit 后的本机 Operations / Observability /
Diagnostics / Incident / Traceability / CI 收成一条**本机优先、Orange Pi 后置**的实施合同。
一次只激活内部一个 LD。不得连接 Platform、操作 physical `can0` / `/dev/ttyS7`，也不得把
Freeze 的实物缺项预填为 PASS。

## 1. 选择与激活规则

仓库任意时刻只能有一份 Current Gate。本文已于 2026-08-18 按用户选择 B 激活，同一变更中：

1. 将本文状态改为 `Active`；
2. 在 [`plans/README.md`](README.md) 中把本文标为唯一 Current Gate；
3. 把 Closed-Loop Portfolio Freeze 标为 `Deferred / still open`，不能写成 Closed；
4. 冻结 Runtime 代码基线 SHA `3c3bba4...`；implementation baseline 为
   `049894d6fc367743b45de0b846ab73214e736a95`；
5. 一次只激活本文内部一个 Local Development Milestone；LD0/LD1/LD2/LD3/LD4/LD5/LD6/LD7
   的 source/test acceptance 已记录，用户明确选择完成 LD8。该选择只允许形成 local release
   candidate，不能启动 Platform、Orange Pi 或 physical Gate。

本文全部本机退出条件完成后，才允许重新选择 Closed-Loop Portfolio Freeze 或另写精确的
Orange Pi Acceptance Gate。切换 Gate 不删除、降级或伪造已有物理证据。

## 2. 目标与非目标

### 2.1 目标

在不依赖 Orange Pi 和实物接线的前提下，把当前 Runtime 收成一个可部署、可观察、可诊断、
可回滚、可验证的 Linux service release candidate：

```text
current audit/evidence baseline
→ confirmed Runtime gap decision
→ local Operations Plane
→ local read-only Observability
→ offline Python diagnostics
→ repeatable incident drills / RCA
→ requirements traceability
→ thin CI / optional provisioning dry-run
→ clean local release candidate
→ later Orange Pi / physical acceptance
```

### 2.2 非目标

本文不授权：

- EtherCAT、ROS 2、PREEMPT_RT、新 UI、新总线、新执行器或新仓库；
- `ITransport`、FieldbusManager、plugin framework、统一大 Reactor 或 Runtime Core 大重构；
- 新运动控制、安全控制、自动恢复策略或旧 command/ACK/session 重放；
- Platform/Dashboard 实时控制、第二套 Runtime state authority 或伪造 metrics；
- 为展示技术而增加 REST、gRPC、消息队列、Kubernetes、Terraform 或 Runtime container；
- 把本机 vcan、loopback、static unit verify 或 dirty evidence 写成 Orange Pi / physical acceptance；
- 在本文阶段操作 physical `can0`、`/dev/ttyS7`、MR0、STM32 wiring 或 Orange Pi boot assets。

## 3. 平台与硬件选择

本 SPEC 不采购新硬件。

| 角色 | 已选目标 | 本 SPEC 中的用途 | 本 SPEC 不证明 |
|---|---|---|---|
| 本机开发 | ThinkPad x86_64 Ubuntu | fresh build、vcan、systemd 用户态合同、diagnostics、CI | ARM、physical CAN/RTU |
| 可选隔离 | 本机 network namespace 或 Ubuntu VM | vcan、坏配置、upgrade/rollback、provisioning dry-run | Orange Pi BSP |
| 后置部署 | Orange Pi 4 Pro 4GB | 本 SPEC 完成后的 ARM/systemd/physical acceptance | 本机阶段不操作 |
| 后置 CAN | can2 kernel + MCP2515 `can0` + STM32F103/SN65HVD230 | 后置 physical Gate | vcan 不替代 |
| 后置 RTU | UART7 `/dev/ttyS7` + RS-485 HAT + MR0-IOR08 | 后置 physical Gate | PTY/loopback 不替代 |

Orange Pi 上仍是普通 Linux。即使后续使用 `SCHED_FIFO`，也不等于 MCU RTOS、PREEMPT_RT 或
硬实时保证。

## 4. 当前事实与 Phase 3 决策

HEAD audit 已完成 process/thread/fd/event 模型，本机 batch 已观察：

- `test_runtime_daemon` 20 个外层进程与 `test_rcrd_process` 10 轮通过；
- duration/eventfd 与 SIGTERM/signalfd 关闭顺序符合当前模型；
- CPU stress 下普通 Linux deadline misses/tail 增加，但没有证据指向 Runtime 代码缺陷；
- 重复启停测试曾观察到 `/proc` 线程计数短暂多 1；通过 250 ms 有界收敛观察后，10 次重复运行均回到基线，fd 无增长；
- interface-down case 为显式未授权 skip；isolated netns 为 `permission_denied`。

因此 Phase 3 / LD1 默认结论是：**No confirmed Runtime OS code gap / no C++ change**。

只有后续出现稳定、可复现的 unsafe shutdown、fd/thread leak、错误时钟、restart state leak、
blocking violation、race、stale command 或错误传播缺陷，才允许按以下格式申请最小修复：

```text
Before
Observed Problem
Facts / Unknowns
Root Cause
Selected Change
Rejected Alternative
After
Regression
Residual Risk
```

一次偶发现象、旧 evidence 或“可能更优雅”不能触发 Runtime Core 重构。

## 5. 稳定 ownership 与依赖方向

```text
rcr (Runtime/Core/Linux mechanisms)
  ↓
rcr_workbench (headless application/services)
  ↓
optional Qt

deploy / operations scripts ── manage processes and artifacts only
linux/scripts/diagnostics   ── read exported files/logs only; not a root product
Platform / Edge Agent      ── deferred; no Runtime authority in this SPEC
```

| 对象 | 唯一 owner | 后续开发不得做什么 |
|---|---|---|
| Runtime mode/fault/watchdog/ACK | `LinuxRuntime` | healthcheck/Platform 复制判断 |
| CAN fd/epoll/I/O thread | `CanIoLoop` | script、Qt、status tool 打开第二个控制 socket |
| node heartbeat/session/restart | `NodeSupervisor` | diagnostics 反向修改状态 |
| process lifecycle / exit | `RuntimeDaemon` + systemd host | Python 管控制生命周期 |
| CEL1 status/command boundary | `rcr_cell_app` / Workbench protocol | 增加未定义运动控制入口 |
| serial `/dev/ttyS7` | `rcr_modbus_rtu_agent` | cell app、Qt、healthcheck 直接打开串口 |
| analysis result | `linux/scripts/diagnostics/` output | 冒充 Runtime source of truth；不在仓库根新建 `diagnostics/` |

## 6. 候选 service topology

本机阶段必须区分两种替代宿主：

```text
standalone software/service path:
  rcr-vcan.service → rcrd.service

portfolio main-demo path:
  rcr-modbus-rtu-agent.service
              ↑ After/Wants; not hard Runtime authority
  rcr-cell-app.service ── Conflicts with rcrd.service
```

冻结原则：

- `rcrd` 与 `rcr_cell_app` 不能同时写同一 CAN interface；systemd 合同必须显式防止双 owner；
- agent offline 不应让 Runtime/CAN supervision 消失；cell app 应报告 Cell I/O degraded/offline；
- Qt `--cell-peer` 是外部工程站，不作为 Orange Pi service dependency；
- 本机可用 vcan/loopback 验证 lifecycle，但 agent 无 physical serial 时必须标
  `MOCK/LOOPBACK/NO PHYSICAL RS485`；
- 是否采用 `Conflicts=`、显式 preflight 或进程锁，必须在 LD2 编码前比较。默认优先 systemd
  可见合同，不引入新的跨进程锁协议。

## 7. Local Development Milestones

### LD0 — Baseline Freeze

问题：当前 audit、模型和本机 evidence 位于 dirty worktree，若直接编码会混淆“实验基线”和
“实施后状态”。

输入：HEAD audit、三份模型、本机 evidence batch。  
输出：用户审阅的文档/evidence checkpoint 与 clean implementation baseline。  
允许：文档一致性、链接、manifest/hash、证据边界修正。  
禁止：顺手改 Runtime 或 service。

退出条件：

- `git diff --check`、文档链接和 evidence hash 通过；
- batch 仍标 `LOCAL / VCAN / DIRTY`，不因提交而回写成 clean experiment；
- implementation baseline 是明确的新 clean commit；
- 本机旧 `rcrd.service` 当前 inactive 状态被记录，但不自动重启或删除。

**关闭（2026-08-18）：** 用户提交 `049894d6fc367743b45de0b846ab73214e736a95`，工作树 clean；
`git diff 3c3bba4..049894d -- linux/ firmware/ deploy/ protocol/` 为空。evidence 索引已入库；
`20260818T033609Z` 原始文件仍被 `/evidence/**` ignore（与仓内 measured artifacts 政策一致），
不因入库 README 把 batch 改写成 CLEAN。未自动重启 `rcrd.service`。

### LD1 — Runtime OS Gap Decision

当前预期：`NO CHANGE / SKIPPED BY EVIDENCE`。

只有出现可复现缺陷才进入修改；否则把 audit 结论链接进 acceptance matrix 后退出。不得为了
“每阶段都有代码”制造重构。

退出条件：

- 明确列出 confirmed gap 数量；当前预期为 0；
- 所有 skip/permission_denied 保留原分类；
- 若为 0，Runtime C++ diff 必须为空。

**关闭（2026-08-18）：** confirmed Runtime OS gap = **0**。`NO CHANGE / SKIPPED BY EVIDENCE`。
保留：iface-down `skipped`，isolated netns `permission_denied`，`strace -p` attach
`permission_denied`。相对 `3c3bba4` 无 Runtime C++ diff。细节见
[`evidence/head_reality_audit/README.md`](../../evidence/head_reality_audit/README.md)
与 [HEAD Reality Audit §13](../HEAD_REALITY_AUDIT.md)。不改 C++。

### LD2 — Minimal Operations Plane

#### 目标

让实际两个 Runtime 宿主和 Modbus agent 具备可解释的 install/deploy/start/stop/status/log/
upgrade/rollback 生命周期，不建设通用 package manager。

#### 候选实现包

1. 收敛现有 `deploy/orangepi/install_release.sh` 与 rollback 资产，使 release manifest 覆盖
   `rcr_cell_app`、`rcr_modbus_rtu_agent` 及实际依赖，而不只覆盖旧 standalone 三个 binary；
2. 为 main-demo 候选 unit 定义 service user、group、路径、配置、设备权限、restart/stop policy；
3. 保留 standalone `rcrd.service`，但与 cell app 建立显式互斥；
4. 提供少量窄脚本或一个子命令式 operations script，覆盖 deploy/status/healthcheck/
   collect-logs/upgrade/rollback；不机械创建十个重复 wrapper；
5. release manifest 至少记录 commit、dirty、compiler、arch、build type、binary SHA-256、配置版本；
6. rollback 只切换到已存在的 last-known-good，不删除 release，不吞掉 restart/healthcheck 失败。

#### Health contract

必须分开输出：

```text
process_alive
service_active
runtime_reachable
runtime_state
device_health
cell_io_health
version_match
```

状态源不可用时返回 `UNKNOWN/UNAVAILABLE`，不能由 process alive 推断 runtime/device healthy。
healthcheck 只读，不打开 control CAN fd、不写 Modbus、不 Activate、不清 fault。

#### Collect-logs contract

incident bundle 至少尝试收集：

- UTC/monotonic anchor、host/kernel/arch；
- Git/release manifest/current symlink；
- unit show/status 与 bounded journal；
- process/thread/fd/scheduler snapshot（权限不足保留错误）；
- CAN interface只读状态；
- CEL1 现有 status（若服务可达）；
- 配置摘要、latest final summary、collector 自身 error list；
- 文件 hash。

collect failure 不得覆盖已经收集的部分；bundle 必须标 complete/partial。

#### 本机验证

- fresh install root 或 `/tmp` prefix 的 dry-run；
- `systemd-analyze verify` / unit static verification；
- standalone vcan service lifecycle；
- main-demo 只跑 `VCAN / LOOPBACK / NO PHYSICAL RS485`；
- v1 healthy → bad v2/config → healthcheck fail → rollback v1 → healthy；
- 精确 PID/service target，不使用 killall 或宽泛清理。

退出条件：本机相同 clean commit 可重复完成 install、status、healthcheck、bundle、upgrade 和
rollback；不得据此声称 Orange Pi installed/healthy。

**实现记录（2026-08-18，本机 dirty worktree）：** 已扩展 release manifest 与不可变目录，
纳入 `rcr_cell_app`、`rcr_modbus_rtu_agent`、运维 CLI、只读 CEL1 probe、rollback 脚本和五个
systemd unit 快照；`rcrd.service` 与 `rcr-cell-app.service` 使用双向 `Conflicts=`，agent
使用 `dialout`/`DeviceAllow=/dev/ttyS7`，且不被 cell app `Requires=`。healthcheck 缺少状态源时
返回 `UNKNOWN/UNAVAILABLE`，bundle 保留 partial 和 error list。

本机结果：`systemd-analyze verify` 五个 unit 通过；Qt-OFF CTest 在允许 localhost socket
的环境中 **34/34 passed**；受限 sandbox 首轮的两个 localhost loopback failure 已复核为
`permission_denied`，不是测试断言失败。临时 prefix 已完成两个 release 的 install/status/
partial bundle/rollback；fake systemd + CEL1 loopback 已复现 healthy → version mismatch fail
→ rollback。所有结果仍是 `LOCAL / LOOPBACK / DIRTY`，未操作 Orange Pi，也未形成 physical CAN/RS-485
acceptance。

### LD3 — Local Read-Only Observability

#### 数据源分级

| 指标 | source of truth | 当前可取得方式 | 缺失处理 |
|---|---|---|---|
| process/service uptime/restart | systemd/kernel | `systemctl show`、`/proc` | unavailable |
| version/arch/config | release manifest | current symlink + manifest | mismatch/unknown |
| Runtime mode/fault | `LinuxRuntime` projection | existing CEL1 status / final summary | 不从日志猜实时状态 |
| node online/input/fault | `NodeSupervisor` projection | existing CEL1 status | stale/unknown |
| CellReady/DO0 requested/confirmed | edge cell app | existing CEL1 status | offline/unknown |
| scheduler/ACK/I/O counters | `DaemonSnapshot` | current internal snapshot/final log | 未稳定导出则 `not_available` |
| host CPU/memory/temp | kernel/sysfs | existing edge-agent collector or local command | 不冒充 Runtime metric |

#### 选择

第一 slice 优先复用 existing CEL1 `GetStatus` 与 systemd/manifest，不改协议。若必须暴露
`DaemonSnapshot` 中未导出的字段，先写接口合同并单独评审兼容性；不得直接序列化私有 C++ struct。

可增加一个窄的 headless read-only consumer，前提是复用 `CellAppClient`、不提供 control command、
不打开 CAN/serial。备选是扩 Qt 或接 Platform；前者把运维依赖 GUI，后者扩大跨仓 scope，均不选。

Platform/Edge Agent 在本 SPEC 保持 deferred。尤其不能继续把固定字符串 `runtime_state="idle"`
写成真实 Runtime metric。

退出条件：每个输出字段都有 owner、采样时刻/age、unknown 语义和可复现测试；没有 source 的字段
不发布。

**实现记录（2026-08-18，本机 dirty worktree）：** 新增 `rcr_observe.py` 与
`rcr_operations.sh observe`。它复用 CEL1 `GetStatus`、current/MANIFEST 和 systemd/kernel
只读源，输出 `rcr.local_observability.v1` JSON；结果按 host、release、service、runtime 分组，
每组标 owner、availability、采样时刻或 monotonic age。远端 CEL1 不跨主机混算 monotonic age，
缺失的 systemd、manifest、CEL1 source 输出 `UNKNOWN/UNAVAILABLE`，不从 process alive 推断
Runtime/device health。输出包含 Runtime mode/fault、Node heartbeat/input/fault、通信计数、
ACK、CellReady 和 DO0 requested/confirmed/status；没有新增控制入口、CAN fd 或串口 owner。

本机 fixture 已验证：安装后的 release CLI 可输出 schema 正确、CEL1 available、release version
match 的 JSON；空 prefix 可输出结构完整且 Runtime/release 为 unavailable/unknown；systemd 查询
有界为 1 s。结果仍为 `LOCAL / LOOPBACK / DIRTY`，未形成 Orange Pi 或 physical acceptance。

### LD4 — Python / pandas Diagnostics

Python 只读已导出的 evidence/log/CSV/JSON，不进入 Runtime 进程、不做 control decision。
**不在仓库根目录新建 `diagnostics/`。** 根级名字更显眼，但会让离线分析看起来像与
`linux/`、`deploy/` 平级的产品，也容易被误当成 Runtime 进程内诊断。顶层
`linux/`、`protocol/`、`deploy/`、`experiments/`、`evidence/`、`docs/` 已经冻结，不为发现成本
再开第七个系统根。

LD4 若被激活，脚本放在 `linux/scripts/diagnostics/`：与现有 verification 辅助同类，不链进
`rcr` 库，不由 Linux CMake 当 Runtime target 构建。发现靠 `linux/scripts/` 与 evidence 索引，
不靠新顶层目录。

不选：

- 根 `diagnostics/`：短路径，ownership 混；
- `experiments/diagnostics/`：像一次性实验，但 incident replay 会成为运维合同的一部分；
- `docs/diagnostics/`：文档树不适合跑 fixture tests。

建议最小目录：

```text
linux/scripts/diagnostics/
├── parse_runtime_trace.py
├── summarize_run.py
├── build_incident_timeline.py
├── compare_runs.py
└── tests/fixtures/
```

输入优先使用稳定、真实存在的格式：benchmark 输出、journal export、final summary、incident
bundle metadata、CEL1 status capture。没有稳定 JSONL trace 时不得先写虚构 parser。

输出：

- machine-readable JSON/CSV；
- human-readable Markdown summary；
- timing 的 count/mean/P50/P95/P99/max（仅数据存在时）；
- restart/fault/timeout/deadline miss 计数；
- session/sequence gap 和 timeline；
- 缺列、坏行、时钟域、单位与 partial input 警告。

实现选择：少量日志用 Python 标准库；只有表格聚合/compare-runs 明显获益时才引入 pandas。
不新增 Web frontend。

退出条件：fixture tests、坏输入 tests、确定性输出、schema/version 字段和至少一个真实本机 bundle
回放通过；分析结论仍区分 Fact/Hypothesis/Conclusion。

**实现记录（2026-08-18，本机 dirty worktree）：** 新增 `linux/scripts/diagnostics/`：
`parse_runtime_trace.py` 只读取 RuntimeDaemon 已有的 final-summary 行；`summarize_run.py` 读取
LD5 的 `environment.txt`/`RESULTS.txt`/可选 benchmark；`build_incident_timeline.py` 只输出
runner 顺序并警告没有 per-scenario timestamp；`compare_runs.py` 只比较两份 versioned summary
的数值差。它们没有 socket、CAN、serial、systemd control 或 Runtime import。当前固定小 schema
没有引入 pandas；这是避免无真实表格收益的依赖，而不是宣称 pandas 不适合后续稳定大矩阵。

本机结果：fixture 确定性、坏 final-summary 输入非零退出均通过；新 LD5 batch
`20260818T141251Z` 的 final-summary、summary、timeline 和 self-compare replay 通过。结果仍为
`LOCAL / VCAN / LOOPBACK / DIRTY`，不形成 physical/Orange Pi acceptance。

### LD5 — Local Incident Drills and RCA

本机阶段选择 5 个可真实执行的场景：

1. Runtime SIGKILL / systemd restart / new process generation；
2. bad config 或 bad release → healthcheck fail → rollback；
3. vcan peer heartbeat/ACK drop 或 delayed response；
4. Modbus agent unavailable / loopback timeout（`NO PHYSICAL RS485`）；
5. CPU overload → scheduler tail/deadline miss observation。

interface down、ptrace attach、network namespace 等需要权限的场景必须单独授权；不可绕过。STM32
timeout、physical CAN/RS-485 disconnect 留给 Orange Pi/physical Gate。

每个 incident 写入 `docs/incidents/`，统一字段：

```text
Symptom
Facts
Unknowns
Hypotheses
Experiment
Evidence
Root Cause (only if proved)
Recovery
Fix (or No Code Change)
Regression
Residual Risk
```

退出条件：5 个 drill 都有精确命令、环境、退出码、evidence 和恢复；未证明 root cause 的文档只
停在 hypothesis/conclusion，不填虚假 Root Cause。

### LD6 — Requirements / Verification Traceability

最小要求集：

| ID | Requirement |
|---|---|
| `REQ-001` | 超过 deadline 的 command 不得执行或在 restart 后重放 |
| `REQ-002` | device loss 必须进入明确 degraded/Hold/Fault 语义，不能维持假 healthy |
| `REQ-003` | Platform/网络失败不得改变 Runtime 本地 control authority |
| `REQ-004` | Runtime restart 后旧 command/ACK/session 不得被误用 |
| `REQ-005` | 新 release/config healthcheck 失败时可回滚 last-known-good |
| `REQ-006` | health 输出必须区分 process/service/runtime/device/cell I/O |

生成 `docs/REQUIREMENTS_TRACEABILITY_MATRIX.md`，每行必须链接：

```text
Requirement
→ Interface/ownership contract
→ implementation location
→ unit/integration test
→ fault/incident drill
→ raw evidence
→ acceptance status and environment
```

缺一环就标 partial/not_run，不用文档链接替代执行证据。

**实现记录（2026-08-21）：** 已生成 [`docs/REQUIREMENTS_TRACEABILITY_MATRIX.md`](../REQUIREMENTS_TRACEABILITY_MATRIX.md)。
六条需求逐行链接 interface/ownership contract、实现位置、测试、LD5 incident、raw evidence 和
acceptance/environment 状态。`REQ-001`、`REQ-002`、`REQ-004` 为本机 Runtime/vcan 软件路径验证；
`REQ-005`、`REQ-006` 为临时 prefix/localhost loopback 验证；`REQ-003` 因 Platform deferred
保持 `PARTIAL / NOT_RUN`。没有新增 C++、Platform、CAN fd、串口 owner 或 health authority。

### LD7 — Thin CI / Provisioning Draft

CI 只覆盖：

```text
fresh Qt-OFF build + CTest
optional Qt-ON build/test
diagnostics unit tests
shell/static/document checks
release artifact + manifest
```

不自动操作 host vcan/systemd/physical devices；需要 privilege 的测试保留显式 job/skip reason。
不建设 CD、Kubernetes 或 Runtime Docker image。

Ansible 只有在 LD2 install/health/rollback 合同稳定后才允许进入：

- 第一实现目标是 Ubuntu VM/check mode 或 disposable local target；
- 只封装已经人工验证的步骤，不重新发明 deployment policy；
- Orange Pi inventory 与实际 apply 全部留给后置 Gate；
- 不购买第二块板子。

退出条件：CI 可生成同一 SHA 的 artifact/manifest；provisioning draft 在非板环境通过 syntax/check
或明确 unsupported。它不是 Orange Pi deployment evidence。

### LD8 — Local Release Candidate Gate

汇总产物：

- `docs/LOCAL_SYSTEMS_ENGINEERING_ACCEPTANCE_REPORT.md`；
- `docs/REQUIREMENTS_TRACEABILITY_MATRIX.md`；
- 5 个 incident RCA；
- release artifact/manifest/hash；
- local Operations/Observability/Diagnostics/CI evidence；
- open gaps 与 Orange Pi entry checklist。

退出条件：

- 同一 clean commit、fresh build、全部非特权测试通过；
- local vcan/service/rollback/incident evidence 可重复；
- 没有把 skip、permission_denied 或 dirty result 升级；
- Runtime Core 只有 confirmed gap 对应的最小 diff；预期可为零；
- 本机服务、临时负载和进程均回到记录的终态；
- 用户明确选择下一 Gate 前停止。

**实现记录（2026-08-21）：** 已生成 [`docs/LOCAL_SYSTEMS_ENGINEERING_ACCEPTANCE_REPORT.md`](../LOCAL_SYSTEMS_ENGINEERING_ACCEPTANCE_REPORT.md)。
它汇总 LD6 traceability、五类 LD5 incident、LD7 CI/provisioning、release manifest/hash 和
Orange Pi entry checklist。最终 clean commit 上的 Qt-OFF/Qt-ON fresh matrix、非特权测试、
Operations temporary-prefix 合同和静态检查均须由 `CI_SUMMARY.txt` 与 manifest/hash 对齐；
physical CAN/RS-485、Platform 和 Orange Pi 结果保持 `NOT_RUN`。

## 8. 后置 Orange Pi Acceptance

本 SPEC 不执行 Orange Pi 实验，只冻结进入条件和已有硬件。

### 8.1 Entry criteria

- LD8 已关闭并形成 clean release candidate；
- artifact/manifest/hash 与本机 acceptance 同 SHA；
- install/healthcheck/collect/rollback 已在本机或 disposable target 验证；
- Orange Pi 当前 kernel flavor、DT、can0、ttyS7、占用和 rollback 重新只读核验；
- physical wiring/power/termination/设备站号按现有硬件文档冻结；
- 新 Gate 明确哪些动作需要用户现场操作或 root 权限。

### 8.2 Later board matrix

| 项 | 后置验证 |
|---|---|
| ARM build/artifact | aarch64 native/install，同一 SHA |
| service lifecycle | cell app/agent start/stop/restart/boot/permissions |
| CAN ownership | `rcr_cell_app` 唯一写 `can0`，不并行 `rcrd` |
| physical node | STM32 heartbeat/status/ACK/lease/CommLoss |
| physical RTU | agent 独占 `/dev/ttyS7`，MR0 requested/confirmed |
| current Freeze gap | RS-485 掉线瞬间、恢复需 Probe、旧 DO0 不重放 |
| pressure | 普通 Linux ARM 样本；不预设优于 ThinkPad |
| upgrade/rollback | bad release/config 后恢复 last-known-good |
| evidence | environment、journal、trace/status、hash、照片/录像、dirty/clean |

本机通过只允许作为 entry prerequisite，不允许预填上述 board 结果。

## 9. 验证矩阵

| 能力 | 本机自动 | 本机人工/特权 | 后置 Orange Pi |
|---|---|---|---|
| build/unit/codec/core | required | — | native rerun |
| Runtime vcan lifecycle | required | namespace/interface-down 按授权 | physical CAN rerun |
| systemd unit syntax | required | live service 按授权 | required |
| main-demo topology | vcan/loopback only | optional local service | required physical |
| serial/Modbus | codec/loopback | PTY 只能 software | `/dev/ttyS7` + MR0 |
| health/bundle | required | `/proc`/journal permission recorded | required |
| upgrade/rollback | temp prefix / VM | live unit 按授权 | required |
| diagnostics/RCA | required fixtures + real local bundle | — | replay board bundle |
| CPU pressure | ordinary Linux sample | stress tool | ARM ordinary Linux sample |

## 10. 失败行为

- build/test 失败：停在当前 LD，不推进后续模块；
- health source missing：输出 unknown/unavailable，不能返回 healthy；
- bundle partial：保留已收集文件与 error list，不伪造 complete；
- upgrade health fail：停止新版本，回滚明确目标；回滚失败即终止，不继续清理/删除；
- diagnostics bad input：非零退出并指出文件/字段/单位，不静默丢行；
- incident 无法安全复现：标 blocked/not_run，不用 Mock 代替 physical claim；
- 新发现需要协议/ownership 变化：停止当前 LD，回到 SPEC 评审，不在实现中静默扩 scope。

## 11. 方案比较

| 选择 | 采用 | 不选方案与原因 |
|---|---|---|
| 本机先形成 release candidate | 是 | 直接上板边开发会混合代码、权限、BSP、CAN/RTU 和接线故障 |
| Operations 复用现有 release/systemd | 是 | 新 package manager 或容器化 Runtime 增加部署语义 |
| CEL1/systemd/manifest 本地只读观测 | 是 | 立即接 Platform 会扩大跨仓 authority，Qt-only 又依赖 GUI |
| diagnostics 离线读 evidence | 是 | Python 进入 Runtime 或控制路径破坏语言/线程边界 |
| `linux/scripts/diagnostics/` | 是 | 根目录 `diagnostics/` 更显眼，但会像第六个产品根，且与已冻结顶层布局冲突 |
| 最小 CI | 是 | CD/Ansible actual board apply 在合同未稳定前只会自动化未知步骤 |
| confirmed gap 才改 Runtime | 是 | 为完成阶段而重构已经通过实验证明的机制没有工程收益 |

## 12. 实施时的模块说明要求

本文将来被激活后，每个模块编码前仍必须向用户说明：

- 解决的问题与 observed evidence；
- 所属层和为什么不属于其他层；
- 输入、输出、数据流、线程/时间模型；
- fd/file/process/config ownership；
- 失败、关闭、rollback 行为；
- 当前选择与至少一个合理备选；
- 单元、集成、本机 evidence 和后置 board 验证方法。

代码完成后同步更新 `docs/KNOWLEDGE_BASE.md`，并解释实际调用链、状态变化和不能声称的能力。

## 13. 首个实施包（仅在本文激活后）

严格顺序：

1. LD0：审阅并固化当前 dirty audit/evidence baseline；**已关闭**（`049894d`）
2. LD1：记录 `NO CONFIRMED RUNTIME GAP / NO C++ CHANGE`；**已关闭**（gap = 0）
3. LD2-A：先写 Operations contract 与 service topology 设计；**已完成**
4. 用户审阅 LD2-A 后，实现最小 install/status/health/bundle/rollback slice；**已完成**
5. LD3：实现本机只读 Observability；**已完成，clean acceptance commit 待提交**
6. LD5：实现并执行本机五类 incident drill，形成 `docs/incidents/` RCA 与原始 evidence；
   **本机 batch 通过；source/test acceptance 已记录于 `28bf3eb`，raw evidence 保持 DIRTY**。
7. LD4：实现离线 final-summary/run-summary/timeline/compare 脚本，fixture 与真实 LD5 batch replay
   通过；**source/test acceptance 已记录于 `28bf3eb`，raw evidence 保持 DIRTY**。
8. LD6：生成六项需求追踪矩阵，逐行完成合同、实现、测试、incident、raw evidence、状态链路；
   **本机完成，REQ-003 保持 PARTIAL / NOT_RUN**。
9. LD7：建立 Qt-OFF/可选 Qt-ON fresh build、CTest、diagnostics、文档/脚本检查、release
   artifact/manifest/hash 和 Ansible check-only draft；**本机验收完成，物理/Platform 未运行**。
10. LD8：汇总本机 acceptance report，在同一 clean commit 上重跑 release-candidate 矩阵并
    对齐 artifact/manifest/hash；**本机验收关闭，等待后置 Orange Pi/physical Gate**。

## 14. Stop Rules

- LD8 已关闭：不接 Platform、不操作 Orange Pi/physical hardware；停止，等待用户选择后置
  Orange Pi/physical Gate。
- LD5 的 live systemd restart、interface down、ptrace attach、network namespace 仍按权限边界
  标为 `NOT_RUN`；不能用本机进程演练替代它们，也不能把 VCAN/loopback 写成物理 acceptance。
  LD0/LD1/LD2/LD3/LD4/LD5 的 raw run 仍保持 dirty local classification；不得把 source/test
  acceptance 改写成 physical 或 clean evidence。
- 一次只推进一个 LD；退出条件未关，不进入下一个；
- 不因“Orange Pi 稍后做”而用本机 evidence 预填 board acceptance；
- 不因 P3 无代码变更而补无需求抽象；
- 不把 Platform、Dashboard、Qt 或 Python 变成 Runtime control authority；
- 不自动重启当前已停止的本机旧 `rcrd.service`；
- 不修改 physical Gate 状态，除非用户明确完成 Gate 切换；
- LD8 完成后必须停，等待用户选择 Orange Pi/physical Gate。
