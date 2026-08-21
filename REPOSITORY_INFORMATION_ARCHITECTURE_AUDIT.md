# Repository Information Architecture Audit

> **Historical audit / superseded for current-state claims.** 本文记录 2026-08-11 的目录与导航
> 审计，不决定当前 HEAD、Current Gate 或下一项工作。当前项目状态只读
> [docs/plans/README.md](docs/plans/README.md)；对外集成阅读入口是
> [docs/portfolio/SOLUTION_CASE.md](docs/portfolio/SOLUTION_CASE.md)。

审计日期：2026-08-11  
审计范围：仓库根导航、Runtime/Workbench 代码、协议、实验、部署、证据、计划、作品集与归档文档  
审计基线：`main` / `284152b`，审计开始时工作树干净且与 `origin/main` 同步

## 1. Executive summary

这个仓库的一级目录和 Runtime 代码分层是合理的，不需要重新设计。`linux/`、`protocol/`、
`deploy/`、`experiments/`、`evidence/`、`docs/` 各自承担不同生命周期的职责；`firmware/`
也明确是可选实验边界，而不是 Linux 构建的一部分。

仓库“显得乱”的主要原因不是代码散乱，而是长期开发留下了多套信息坐标：

- 语义名称与旧阶段编号并存，例如 Orange Pi bring-up 同时出现 `P3-A*`、`P3-B*`；
- Architecture、Runtime 原理、职责分区、daemon 合同分别有文档，但缺少明确的 authority
  和阅读顺序；
- Current Gate、未来 physical CAN candidate、长期 roadmap 都包含阶段表，入口不明确时像三套
  “当前计划”；
- `evidence/` 大体按能力组织，但存在 `orangepi`/`orangepi_baseline`、
  `workbench`/`qt_workbench` 等历史命名；
- 学习资料和历史执行记录体量很大，虽然有价值，但在入口层与当前事实的距离不够明显；
- `docs/LINUX_RUNTIME.md` 仍有三处把已实现的 I/O/daemon 路径描述为未来工作，属于真实陈旧信息。

因此本轮应采用“补入口、定 authority、修陈旧事实、不搬稳定路径”的方案。代码、协议、证据
目录和部署脚本均不应因视觉整齐而迁移。

## 2. Current structure

| Repository area | 当前职责 | 审计判断 |
|---|---|---|
| `linux/` | 独立 CMake 工程；Runtime、Linux fd、SocketCAN、Workbench 与测试 | **Keep**。边界清楚 |
| `protocol/` | 已冻结的 CAN V1 线级合同 | **Keep**。不得并入实现 |
| `deploy/` | systemd、release/install/rollback、Orange Pi bring-up 与恢复工具 | **Keep**。工具较多但职责仍属目标机运维 |
| `experiments/` | Modbus TCP、multibus observer、realtime 等独立实验 | **Keep**。没有冒充 Runtime feature |
| `evidence/` | 按能力和历史批次保存验证产物 | **Keep**。命名不完全统一，但迁移风险高 |
| `docs/` | 合同、架构、运维、验证、学习、计划、作品集、归档 | **Keep**，重点改善导航和 authority |
| `firmware/` | ESP32/STM32 可选实验边界说明 | **Keep**。不进入 Linux CMake |
| `cmake/` | 共用 CMake helper | **Keep** |

### 2.1 Linux code structure

`linux/src` 已按 `core`、`can`、`linux`、`runtime`、`supervision`、`daemon`、`sim`、
`workbench` 分组。include 搜索和 CMake source list 未发现反向 Qt 依赖或循环所有权：

```text
Runtime public capability
        ↓
    rcr_workbench
        ↓
Qt Device Workbench (optional)
```

`RCR_BUILD_QT_DEVICE_WORKBENCH=OFF` 仍是默认值。Qt target 只在选项开启时创建；Runtime
Core 不包含 QObject、Qt event loop 或 Qt fd ownership。

### 2.2 Build and deployment references

- `linux/CMakeLists.txt` 是 Runtime 和 Workbench source/target 的权威清单；实验使用独立 CMake。
- `install()` 安装 `rcrd`、simulator、benchmark、acceptance/fault tools 和库；Orange Pi release
  脚本显式从 `build/linux` 取产物。
- systemd unit 与 install/rollback 合同一致引用
  `/opt/robot-control-runtime/current/bin/...`。
- `linux/scripts/` 面向开发、测试和 evidence collection；`deploy/` 面向目标机安装、systemd、
  bring-up、内核/启动恢复。没有必须通过搬目录才能解决的职责冲突。
- 全部 shell 脚本通过 `bash -n`；10 个 Python 文件通过只读 AST 解析。

## 3. A–J audit answers

### A. 当前一级目录是否合理

合理。顶层目录按“运行实现、协议、目标机运维、隔离实验、证据、文档”划分，比套用通用
`src/apps/tools` 顶层模板更符合本仓职责。`firmware/` 作为明确停放/可选实验边界也合理。

### B. 当前真正导致仓库显得乱的原因

主要是信息入口和时间维度混杂，而不是物理目录过多：同一主题既有当前合同，又有学习笔记、
旧阶段执行计划和证据编号；新人必须先理解 P1/P2/P3、RT0–RT7 才能判断什么是当前事实。

### C. 哪些属于代码组织问题

未发现需要本轮重构的代码组织问题。潜在认知问题是 public header 仍保持扁平，而 source 已按
责任分组，但现在改 public include path 会制造 API、测试和安装兼容风险，收益不足。

### D. 哪些属于文档导航问题

- 根 README 缺少一张完整的 Architecture / Run / Hardware / Verification / Workbench /
  Roadmap / Portfolio 路由表；
- `docs/README.md` 已有任务式导航，但未显式纳入 repository map、ownership map 和 evidence
  根入口；
- 四份 Runtime 核心文档缺少统一的 authority/阅读顺序；
- `evidence/` 没有根 README；
- `deploy/orangepi/README.md` 没有覆盖同目录内较多的 boot/kernel recovery 工具。

### E. 哪些属于历史阶段材料积累

`docs/archive/`、Workbench phase history、证据目录中的时间戳批次、P1/P2/P3 与 RT 编号、
Orange Pi 早期 bring-up 阶段表均属于可追溯历史。它们应保留，但不应承担当前状态 authority。

### F. 哪些属于命名问题

- `evidence/orangepi` 与 `evidence/orangepi_baseline`；
- `evidence/workbench` 与 `evidence/qt_workbench`；
- 多个对外标题以 `P3-A1`、`P3-A2`、`RT6` 开头，而非语义名称；
- `FIVE_LAYERS_ONE_PLANE` 实际描述的是责任分区，不是严格单向的五层架构。

这些名称有大量历史引用。当前应在入口解释语义，不执行批量重命名。

### G. 哪些文件存在职责重复

| 文件组合 | 重叠 | 应保留的独立职责 |
|---|---|---|
| `ARCHITECTURE` / `LINUX_RUNTIME` / `FIVE_LAYERS_ONE_PLANE` | 都解释 Runtime | 系统结构 / 实现机制 / 责任与证据分区 |
| `ARCHITECTURE` / `RCRD_CONTRACT` | 都提 daemon | 组件关系 / 冻结的进程生命周期与 CLI 合同 |
| 三份 `docs/plans/*PLAN*` | 都有阶段和下一步 | Current Gate / Future Candidate Gate / Long-term Roadmap |
| `KNOWLEDGE_BASE` / `MODULE_KNOWLEDGE_CARDS` | 都是学习材料 | 主题式教程 / 固定模板的模块卡 |
| Orange Pi bring-up 文档与 deploy README | 都有操作说明 | 主合同 / 可执行资产索引 |

这些文件不适合简单合并；合并会丢失合同、学习和历史证据的不同用途。

### H. 哪些文档内容存在重复维护风险

最高风险是“当前执行阶段”和“下一优先级”在 README、SPEC、责任分区、roadmap、bring-up
文档中重复。**审计当时**把当前状态指向 `docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md`；该结论已
过时。当前状态现在只由 `docs/plans/README.md` 负责，其他入口只应摘要稳定边界并链接。其次是
Orange Pi 内核/CAN 状态和 Workbench 验证状态，不应在学习文档中复制为新 authority。

### I. 哪些目录绝对不应该移动

- `linux/src`、`linux/include`：移动会影响 CMake、include、测试和安装接口；
- `protocol/`：线级合同必须与 Linux 类型解耦；
- `experiments/`：必须与 production Runtime 保持隔离；
- `evidence/`：路径本身是历史证据引用的一部分；
- `deploy/systemd`、`deploy/orangepi`：被 unit、脚本和运维文档交叉引用；
- `docs/archive`：已正确隔离历史材料；
- `docs/workbench`：已有清楚的主题入口和内部角色。

### J. 哪些整理动作风险最高

1. 重命名 evidence 子目录：当前每个能力目录有 2–20 个跨文档/脚本引用，且路径进入历史产物；
2. 改 public header 布局或 namespace：会改变消费者接口和 install tree；
3. 搬 `linux/scripts` 与 `deploy/orangepi` 工具：会破坏相对路径、远端操作手册和证据复现；
4. 合并/删除计划和历史文档：会损失决策与证据的时间上下文；
5. 以整理为由修改 CMake target 或 Runtime 依赖：容易破坏 Qt 可选边界；
6. 按新分类重写 evidence 状态：容易把 `unsupported`、`not_run` 或模拟结果误写成通过。

## 4. Findings by severity

| Severity | Problem | Proposed change | Risk | Recommendation |
|---|---|---|---|---|
| High | `LINUX_RUNTIME.md` 把已实现 daemon/I/O 路径写成未来项 | 按当前代码修正文案，不改设计 | Low | **Fix** |
| High | Current Gate 容易被多份阶段文档竞争 | 明确唯一 authority，其他入口链接 | Low | **Fix** |
| Medium | 四份 Runtime 文档阅读顺序不清 | 增加角色与阅读顺序 | Low | **Fix** |
| Medium | 根 README 导航不覆盖七个主要入口 | 增加短路由表 | Low | **Fix** |
| Medium | `evidence/` 无语义入口 | 增加短 README，保留原路径 | Low | **Add** |
| Medium | Orange Pi 工具入口未覆盖 recovery scripts | 在现有 README 分组说明 | Low | **Fix** |
| Low | evidence 目录命名不完全统一 | 入口解释；不迁移 | High if moved | **Keep** |
| Low | 阶段编号出现在标题和证据说明 | 导航优先用语义名称；历史编号保留 | Medium | **Keep / clarify** |
| Low | public headers 扁平、source 分组 | ownership map 解释 | High if moved | **Keep** |

## 5. Documentation classification and authority

### 5.1 Authoritative sources

| Fact | Authoritative source |
|---|---|
| 仓库范围、能力与硬件边界 | `SPEC.md` |
| 系统组件关系和依赖方向 | `docs/ARCHITECTURE.md` |
| 代码模块 ownership | `docs/CODE_OWNERSHIP_MAP.md`（本轮新增） |
| `rcrd` 生命周期、CLI 与退出合同 | `docs/RCRD_CONTRACT.md` |
| CAN V1 wire contract | `protocol/can_v1/` |
| 当前执行阶段（审计当时） | `docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md`；当前状态见 `docs/plans/README.md` |
| physical CAN 候选 Gate | `docs/plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md` |
| 长期开发顺序 | `docs/plans/DEVELOPMENT_ROADMAP.md` |
| Workbench 总览 | `docs/workbench/README.md` |
| Workbench 当前验证条件 | `docs/workbench/GATES.md` |
| Orange Pi bring-up/deployment | `docs/ORANGE_PI_BRINGUP.md` |
| 通用 evidence schema | `docs/EVIDENCE_SCHEMA.md` |
| realtime evidence schema | `docs/REALTIME_EVIDENCE_SCHEMA.md` |
| 作品集对外叙事入口 | `docs/portfolio/README.md` |

### 5.2 Document classification

| Classification | Documents | Judgment |
|---|---|---|
| Contract | `SPEC.md`, `RCRD_CONTRACT`, `HARDWARE_TOPOLOGY`, `OBSERVATION_TO_EXECUTION_CONTRACT`, `protocol/can_v1/*` | 保留；合同各自覆盖范围/进程/硬件/观察到执行/线级协议 |
| Architecture | `ARCHITECTURE`, `ADR-002`, `COMMUNICATION_EVOLUTION`, `FIVE_LAYERS_ONE_PLANE` | 保留；后两者分别是通信演进和责任分区参考，不承担当前 Gate |
| Operations / Bring-up | `ORANGE_PI_BRINGUP`, `ORANGE_PI_CONFIG_CAN_PLAN`, `deploy/**` | 保留；前者为主入口，CAN config 是受限候选路径 |
| Verification / Evidence Guide | `EVIDENCE_SCHEMA`, `REALTIME_EVIDENCE_SCHEMA`, `ETHERCAT_NIC_GATE`, `PREEMPT_RT_FEASIBILITY_GATE` | 保留状态边界；Gate 结果不能被导航摘要覆盖 |
| Learning / Interview Notes | `LINUX_RUNTIME`, `KNOWLEDGE_BASE`, `MODULE_KNOWLEDGE_CARDS`, `ETHERCAT_PROTOCOL_NOTES`, `MODBUS_TCP_NOTES`, `REALTIME_LINUX_LEARNING_PLAN`, `docs/workbench/NOTES` | 保留；不是当前状态 authority |
| Current Gate（审计当时） | `docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md` | 历史记录；不再决定当前任务 |
| Future Roadmap | `V1_PHYSICAL_CAN_EXECUTION_PLAN`, `DEVELOPMENT_ROADMAP`, `COMMUNICATION_EVOLUTION`, `ORANGE_PI_CONFIG_CAN_PLAN` | physical CAN 是 candidate；roadmap 不自动启动工作 |
| Portfolio Narrative | `docs/portfolio/*.md` 及 `assets/README` | 保留；对外叙事必须回链证据，不能反向成为工程事实 authority |
| Historical Archive | `docs/archive/*`, `docs/workbench/archive/PHASE_HISTORY.md` | 保留且退出主导航 |
| Navigation / Asset Index | `docs/README`, `docs/images/README`, `docs/plans/README`, `docs/workbench/README`, `docs/portfolio/README` | 保持短、按任务路由 |
| Isolated mock contract | `docs/workbench/ACTUATOR.md` | 保留；只定义 MOCK profile，不声明实物闭环 |

## 6. Code organization assessment

| Module | Responsibility | Owns | Must not own | Depends on / used by |
|---|---|---|---|---|
| `core` | 通用有界数据结构 | queue storage/overflow semantics | Linux fd、CAN、设备策略 | 被 supervision 使用 |
| `can` | CAN V1 codec 与 node logic | wire encode/decode、纯逻辑状态 | SocketCAN fd、线程、Qt | 被 daemon/sim/workbench adapter 使用 |
| `linux` | POSIX/Linux mechanisms | fd RAII、epoll、eventfd/signalfd、SocketCAN、clock/scheduler primitives | Runtime policy、UI | 被 runtime/daemon/apps 使用 |
| `runtime` | Runtime semantics | state/watchdog/mailbox/trace/ACK/scheduler policy | SocketCAN fd、systemd、Qt | 被 supervision/daemon/workbench 使用 |
| `supervision` | 单节点会话与通信监督 | heartbeat/session/restart/CommLoss decision | 进程生命周期、UI | 依赖 core/runtime/can；被 daemon 使用 |
| `daemon` | composition 与进程生命周期 | workers、stop order、snapshot、组件装配 | wire contract、UI state | 依赖 linux/runtime/supervision；被 apps/workbench adapter 使用 |
| `sim` | vcan node simulation | simulator loop 与 fault injection test surface | production safety claim | 依赖 can/linux；被测试和工具使用 |
| Workbench application | Runtime-facing use cases/adapter | commissioning API/DTO boundary | Runtime ownership、Qt widgets | 依赖公开 Runtime capability；被 services/Qt 使用 |
| Workbench services | headless diagnostics/commissioning services | bounded service workflows | CAN fd ownership、Runtime state machine | 依赖 application；被 tests/Qt 使用 |
| Workbench profile | 隔离配置与 MOCK actuator profile | profile validation/defaults | 实物 actuator claim、Runtime control | 被 Workbench 使用 |
| Qt UI | 可选工程界面 | Qt objects、presentation state、human actions | CAN fd、supervision/safety semantics | 依赖 `rcr_workbench`；默认不构建 |

详细版本应放入 `docs/CODE_OWNERSHIP_MAP.md`，避免把 ownership 继续散写到多个架构文档。

## 7. Scripts, deploy, and evidence assessment

### Scripts and deploy

当前划分可保留：

- `linux/scripts`：本地/CI/目标机均可调用的 Runtime 验证与证据采集；
- `deploy/systemd`：服务单元及静态验证；
- `deploy/orangepi`：release 生命周期、bring-up、CAN kernel/config、U-Boot/串口恢复。

U-Boot/recovery 文件数量较多，但它们只服务 Orange Pi 运维，移动到新 `deploy/tools` 会同时
影响操作文档与人工运行路径。本轮只补索引，不搬文件。

### Evidence

证据总体以 capability 为主，时间戳位于 capability 子目录内。历史上形成的重复语义目录不应
立即合并：路径被最多 20 份文档/脚本引用，而且原始路径有追溯价值。新增根 README 解释：

- 目录名和旧编号是定位标签，不代表当前执行顺序；
- 状态以目录 README/manifest 的 `pass`、`failed`、`permission_denied`、`unsupported`、
  `not_run` 为准；
- vcan、simulator、static verify、普通 Linux 或 dirty-tree 结果不得升级为物理 CAN、功能安全、
  硬实时或正式 release 证据。

## 8. Proposed target structure (Phase 1)

目标结构基于当前仓库，不新增通用模板目录：

```text
README.md                         # 3-minute entry and seven-way navigation
SPEC.md                           # scope/capability/hardware authority
REPOSITORY_INFORMATION_ARCHITECTURE_AUDIT.md
linux/                            # unchanged Runtime and optional Workbench code
protocol/                         # unchanged wire contracts
deploy/                           # unchanged target operations
experiments/                      # unchanged isolated experiments
evidence/
  README.md                       # new semantic evidence index
  ...                             # all historical paths unchanged
firmware/                         # unchanged optional experiment boundary
docs/
  README.md                       # short task-oriented router
  REPOSITORY_MAP.md               # short area/purpose/start-here map
  CODE_OWNERSHIP_MAP.md           # single code ownership map
  ARCHITECTURE.md                 # architecture authority
  LINUX_RUNTIME.md                # implementation/learning guide
  FIVE_LAYERS_ONE_PLANE.md        # responsibility/evidence reference
  RCRD_CONTRACT.md                # daemon contract authority
  plans/                          # explicit current/candidate/roadmap roles
  workbench/                      # existing roles unchanged
  portfolio/                      # narrative, not engineering authority
  archive/                        # historical, absent from main navigation
```

### 8.1 Old → new change table

| Old path | New path | Reason | Risk | References affected |
|---|---|---|---|---|
| no root evidence index | `evidence/README.md` | 解释能力、状态与历史目录边界 | Low | 新增导航引用 |
| ownership scattered across architecture/learning docs | `docs/CODE_OWNERSHIP_MAP.md` | 单一 ownership 入口 | Low | README/docs README 新链接 |
| no short repository map | `docs/REPOSITORY_MAP.md` | 新人快速定位 | Low | README/docs README 新链接 |
| `README.md` current navigation | same path, shorter complete routing | 覆盖七个目标入口 | Low | 无路径变化 |
| `docs/README.md` current navigation | same path, task-oriented routing | 避免成为文件清单 | Low | 无路径变化 |
| four Runtime docs without explicit roles | same paths, authority headers | 避免概念竞争 | Low | 无路径变化 |
| stale future statements in `LINUX_RUNTIME.md` | same path, current factual wording | 与已实现代码一致 | Low | 无路径变化 |
| incomplete Orange Pi asset index | same `deploy/orangepi/README.md` | 暴露 recovery tools | Low | 无路径变化 |

### 8.2 Explicit no-move decisions

| Existing path | Recommendation | Reason |
|---|---|---|
| `linux/src/**`, `linux/include/**` | **Keep** | ownership 已清楚；移动影响 API/CMake/install |
| `protocol/**` | **Keep** | 保持 wire contract 与实现隔离 |
| `deploy/**` | **Keep** | 相对路径和目标机文档引用密集 |
| `experiments/**` | **Keep** | production/experiment 边界可信 |
| `evidence/**` | **Keep** | 历史路径与证据完整性优先 |
| `docs/archive/**` | **Keep / Archive** | 已位于正确的归档位置 |
| `docs/workbench/**` | **Keep** | README/GATES/NOTES/ACTUATOR/history 职责已经合理 |
| any current documents | **No merge/delete** | authority 标注足以解决本轮问题 |

## 9. Phase 0 verification snapshot

- Markdown：扫描 90 个 Markdown 文件，缺失本地链接为 **0**；
- Shell：全部 `.sh` 文件 `bash -n` 通过；
- Python：10 个 `.py` 文件 AST 解析通过；
- CMake/targets：默认 Qt OFF；`rcr_workbench` 依赖 `rcr`，Qt target 位于可选分支；
- Deploy paths：systemd、release install/rollback 和文档均使用冻结的
  `/opt/robot-control-runtime/current` 合同；
- Evidence：未删除、移动或改写任何结果；
- Runtime behavior：Phase 0 未修改代码、协议、CMake、测试或脚本。

## 10. Phase 2 admission decision

本轮允许实施的只有：短导航、authority 标注、陈旧文案修正、ownership/repository/evidence
入口和 Orange Pi 工具索引。目录移动、文件合并、代码/API/CMake/协议/证据迁移均不进入
Phase 2。
