# 七仓作品集统一总纲(SEVEN_REPOS_OVERVIEW)

- **日期**:2026-08-05(基于当日七仓只读审计,详见文末溯源)
- **用途**:招聘经理 5 分钟版"这是一个什么样的工程师" + 技术面 30 分钟版"证据链怎么串"
- **配套**:本仓 `SYSTEMS_SOFTWARE_PORTFOLIO.md`(主仓深度叙事)、`PERSONAL_NARRATIVE.md`(第一人称故事线)、`RESUME_AND_TALK_TRACK.md`(简历条与口述)、`SISTER_REPOS.md`(能力边界,避免重复建设)

---

## 0. 一句话主线

> **七仓构成一条从传感器寄存器到机器人策略闭环的完整证据链:嵌入式感知执行面 → 边缘 Linux Runtime → 具身策略数据治理与分层验证。统一方法论是"证据纪律与诚实边界"——每一句对外声称都能落到代码行或证据文件,不能证明的明确不声称。**

## 1. 七仓总表

| # | 仓库 | 抽象层 | 技术栈 | 叙事角色 | 证据状态 |
|---|------|--------|--------|----------|----------|
| 1 | **robot-control-runtime**(主仓) | 边缘 Linux Runtime | C++20 / POSIX / SocketCAN / SCHED_FIFO | 主工程:周期调度、失败语义、实时性测量 | RT0–RT7 完整证据链,多数为 dirty smoke/pilot(发布 Gate 未全关) |
| 2 | ros2-arm-teleoperation-suite | 控制/总线进程面·上游 | ROS 2 Jazzy / MuJoCo / MoveIt2 / ros2_control | 三仓闭环:执行、遥操作、数据采集、Task GT | 有运行产物;EVIDENCE_INDEX 成熟度待对齐 |
| 3 | robot-arm-episode-data-lab | 数据治理·中游 | Python / 数据管线 / 训练 / 评测 | 三仓闭环:合同、release、训练、评测、handoff | **canonical 集中地**:事实表/止损摘要/权威证据治理最完备 |
| 4 | ros2-moveit-pybullet-bridge | 回放/风险·下游 | ROS 2 / PyBullet / replay harness | 三仓闭环:回放、风险监控、HOC、97 个面试 FAQ | 权威 S4 Hold 证据 + INTERVIEW_PREP(一处真机口径待修) |
| 5 | robot-ops-dashboard | 运维驾驶舱 | FastAPI / 纯 HTML-CSS-JS / WebSocket / MQTT | 系统集成:跨仓 HTTP 契约、监控优先边界 | 7 个 pytest;无 CI;一处跨仓边界 bug(D1) |
| 6 | amr_warehouse_navigation | AMR 仿真 | ROS 2 Jazzy / Gazebo / SLAM / Nav2 | 系统集成:四阶段交付、Mock WMS 任务闭环 | 9 份验证报告;git 历史阶段清晰;worktree clean |
| 7 | ros2-robot-digital-twin | MCU 传感执行面 | STM32F411 FreeRTOS / ESP32 micro-ROS / MQTT | 设备面:三层数据主权、安全工程 | 链路契约代码级核实;git clean 且与远端同步 |

> 七仓**未并成单一产品、不共享进程**,是同一套系统软件直觉在不同抽象层的并列证据(详见 `SISTER_REPOS.md` 与本文 §5 边界)。

## 2. 三组故事

### 故事 A:底层 Runtime 的实时性——"测量过,且知道边界"(主仓,RT0–RT7)

完整故事线(每步都有证据文件,见 `SYSTEMS_SOFTWARE_PORTFOLIO.md`):

1. **普通内核上,同核 OTHER 被 stress 打穿 vs FIFO 0 miss**(RT1 smoke:OTHER miss≈43,530 / p99≈4.0ms;FIFO miss=0 / p99≈9.8µs,A76/cpu7,60s×10 格)——证明调度策略可观测、可量化;
2. **cyclictest 交叉验证**(RT2:OTHER avg 1.28ms vs FIFO avg 4.6µs),工具缺失如实记 unsupported;
3. **用户态抖动源定位**(RT3:mlock 冷触碰 +4097 minflt→0;无 PI ≈78ms vs PI ≈36.8ms);
4. **诚实停在 PREEMPT_RT 门外**(RT4 Gate = Blocked:唯一 uImage、boot/root 同分区、源码↔hash 未闭环——不装核,并说明为什么);
5. **wakeup ≠ e2e**(RT6 分段:baseline p50 wakeup≈60µs / callback≈250ns / e2e≈96µs;cb_busy、io_busy 独立抬高对应段≈500µs);
6. **收口与负面结果清单**(RT7:因果图、证据等级表、未采用优化)。

**面试可讲点**:24 个 CTest 目标、故障矩阵程序 22 场(入库摘要仍记当时 19/19)、7 项 vcan 验收、双平台 12 格调度矩阵、ASan+UBSan 通过、约 1.6 万行 C++(`linux/`)。最独特的是**证据纪律本身**:pilot 不冒充 formal、dirty/clean 分级、environment.txt 全字段、不能声称清单。

**边界**:Not hard realtime · Not functional safety · Not Orange Pi SocketCAN daemon live(板上无 CAN 内核,daemon 生命周期未验证,如实声明)。

### 故事 B:具身策略数据治理与分层验证——"把包装挡在证据链外面"(三仓闭环)

2026-07-27 冻结主语:**具身策略数据治理与分层验证框架**。上游执行/采集 → 中游合同/交付/训练/评测 → 下游回放/风险/监控,为多个策略候选(MLP BC / ACT / SmolVLA / scripted oracle)建立可复现、可审计、防包装的分层判定链路。

**Gate 协议全链一致**(字段级核对通过):上游 `meta.json`(`upstream_gate=batch_generator`、`success=true`)→ 中游 `manifest.json`(`filter_scope=training_split_only`、`physical_validation_applied=true`)→ `handoff_manifest`(`must_validate` 五项)→ 下游静态校验 → `benchmark_summary.json`。

**三个最有说服力的止损案例**(机器可读证据齐全):

1. **错误 evaluator 隔离**(INVALID_EVALUATOR_V0):发现评测器自身错误,隔离而不是掩盖;
2. **interface 5/5 ≠ 任务成功**:SmolVLA 接口全通,但 continuous GT 0/20 → 主动降级判定;
3. **近黑 reach 3/5 主动证伪为 1/5**:修光复测(relight)后权威结果为 reach 1/5 · grasp 0/5 · lift 0/5 → **Hold**,首轮数据标注 Superseded,抽查 9 份引用文档无一处把旧证据当权威。

**边界**:Not task success · Not Sim2Real · Not real robot;`PolicyRunner` 是 replay harness(代码级强制 `is_closed_loop=false`);SmolVLA 默认停止(不扩种子/不重训/不上真机)。

### 故事 C:系统集成与阶段化交付——"把大目标切成可验证的阶段"(AMR + Dashboard + 数字孪生)

1. **AMR 四阶段**(amr_warehouse_navigation,commit 历史 2026-03-09→05-27 清晰可追溯):建图闭环(V1)→ Nav2 导航基线(V2)→ 固定任务点(V2.2)→ 最小 Mock WMS 任务闭环(V3,SQLite/CLI/HTTP/executor),每阶段一份验证文档(9 份 WMS 报告),`warehouse.yaml` 与设计文档逐字节一致;
2. **跨仓契约对齐**(robot-ops-dashboard × amr):dashboard 调用的 3 个 HTTP 端点全部匹配、payload `{target_name, task_name}` 契约一致、状态映射覆盖 amr 全部 5 种状态——"监控优先 + 显式受限交互"(电机命令硬限幅 RPM≤80/PWM≤0.25)是架构取舍;
3. **一仓跨三层的设备面**(ros2-robot-digital-twin):STM32F411 FreeRTOS(100Hz 采样、10 样本 RMS 状态判别)→ ESP32-S3 双核 micro-ROS(UDP 自定义传输、best_effort/reliable QoS 分流)→ ROS 2 → MQTT 归一化桥,链路契约(IMUQ/State:/CMDVEL/九条 topic)全部代码级核实;安全工程:上电即急停、200ms 命令超时停车、双端 max_pwm 钳位、硬件输出默认关闭 + 运行时 arm 门控、PID 按流程调参(kp=0.0030/ki=0.0020)。

**面试可讲点**:阶段化 commit 时间线、文档-证据闭环(design→roadmap→reports)、跨仓 contract 对齐、monitoring-first 边界纪律、嵌入式三层数据主权。

## 3. 统一证据纪律方法论(七仓共性,面试主动讲)

- **dirty / clean 分级**:所有实测标注 git 状态,clean 才升级 formal;
- **pilot / formal 区分**:RT0 矩阵重分类为 pilot,禁止与 RT1 算提升百分比;
- **claims_*=false 文化**:每份 claim 自带"不能证明什么"字段(S4 gate JSON、M6 wiring);
- **Superseded 治理**:旧证据显式标注、relabel、move_to_legacy,不删除但不可当权威;
- **environment.txt 全字段**:内核、调度策略、governor、affinity、负载、温度、权限——"怎么测的"和"测出什么"同等重要;
- **不能声称清单**:每仓 README 显式声明边界(Not hard realtime / Not Sim2Real / Not real robot)。

## 4. 面试素材速查(全部可溯源)

| 素材 | 数字 | 来源 |
|---|---|---|
| 测试/验证 | 18 CTest · 19 故障场景 · 6 vcan 验收 · ASan+UBSan · 5 宿主测试(ESP32) · 7 pytest(dashboard) | ws1 / ws4 / ws3 |
| 实时性 | RT1 OTHER≈43.5k miss vs FIFO 0 · RT2 FIFO avg 4.6µs · RT3 PI 78→37ms · RT6 e2e p50≈96µs | ws1 |
| 策略闭环 | G0–G3 Gate · S4 权威 lift 0/5 Hold · 三次止损 · 97 FAQ | ws2 |
| 系统集成 | AMR 四阶段 · 9 份 WMS 报告 · 3 端点跨仓一致 · 三层链路契约 | ws3 / ws4 |
| 代码量 | ~10.5k 行 C++(linux/) · 14,228 行含实验 | ws1 |

## 5. 整体边界声明(对外统一口径)

- 七仓**未并成单一产品、不共享进程**;引用姊妹仓时用"并列相关工程",不用"附录"。
- Not hard realtime · Not functional safety · Not Sim2Real · Not real robot(除已注明口径的案例外)。
- 所有公开数字当前多为 dirty experiment/smoke/pilot；clean formal 发布 Gate 尚未全部关闭
  （见 [V1 发布 Gate](../plans/PORTFOLIO_V1_RELEASE_PLAN.md)）。
- 待修口径项:下游 INTERVIEW_PREP「案例3 CAN 真机事故」需加口径脚注(审计 ws2-H1);AMR `dock_a`/`start_zone` 边界需统一(审计 ws3-D1/D3)。

## 6. 溯源

- 2026-08-05 七仓只读审计(未修改任何仓库):`~/portfolio-audit/00_SUMMARY.md`(总览)、`ws1_robot-control-runtime.md`、`ws2_three-repo-closed-loop.md`、`ws3_amr-dashboard.md`、`ws4_robot-digital-twin.md`。
- 投递前行动清单(P0–P3)见 `~/portfolio-audit/00_SUMMARY.md` §3。
