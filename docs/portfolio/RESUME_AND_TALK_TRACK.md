# 底层方向：简历条与口述稿

**配套主叙事**：[SYSTEMS_SOFTWARE_PORTFOLIO.md](SYSTEMS_SOFTWARE_PORTFOLIO.md)  
**证据等级口令**：理解过 / 使用过 / 测量过（见 [KNOWLEDGE_BASE.md](../KNOWLEDGE_BASE.md)）

---

## 1. 简历抬头建议

- 目标岗位关键词：`C/C++` · `嵌入式 Linux` · `底层软件` · `中间件` · `Runtime` · `SocketCAN`  
- 一句话简介：

> 自动化背景，独立完成 ROS-free C++ Linux 边缘 Runtime：周期调度、FIFO 可观测降级、epoll/SocketCAN、命令会话合同；在 Orange Pi 上完成原生构建与调度压力对照（普通内核，非硬实时）。

---

## 2. 项目条（可直接粘贴，按此顺序）

### 项目一（主项目）· Linux 边缘 Runtime（本仓）

- 设计并实现 C++20 ROS-free Edge Runtime：`CLOCK_MONOTONIC` 绝对周期、可选 `SCHED_FIFO`（失败显式降级）、`epoll`/SocketCAN、watchdog、状态机、session/sequence/deadline。  
- 普通输出 latest-wins mailbox；恢复后不自动重放。STM32 节点用 PA0 光电确认 `POSITION_REACHED`；边缘 `CellReadyMapper` 映射到 MR0 DO0（requested≠confirmed）。Qt `--cell-peer` 只观察/下发，不拥有 CAN 或自动闭环。  
- ThinkPad/`vcan` 验证软件链；Orange Pi 原生构建与调度对照。闭环实物表在证据入库前保持 NOT RUN。  
- 边界：stock 无 CAN；非硬实时；非功能安全；无 EtherCAT/ROS 2 集成。

### 项目二（相关工程 · 控制 / 总线进程面）

- 臂遥操作栈中区分仿真与真机调度：仿真路径关闭 FIFO，避免高优先级控制线程与普通 DDS worker 形成优先级反转；稳态多速率分层（控制高于观测/策略合同频率）。  
- 虚拟 CANopen DS402 + `vcan` PDO/EMCY 路径，把「驱动状态机 / 心跳 / 故障帧」与本仓 SocketCAN Runtime 面放在同一叙事里讲清分层，而不是写成两个无关 Demo。

### 项目三（相关工程 · MCU 传感执行面）

- STM32 FreeRTOS 姿态与状态判别、ESP32 micro-ROS 桥、N20 编码器电机 PI 速度环台架：证明周期任务与线级合同在 MCU 侧同样成立。  
- 与本仓分工明确：MCU 仓负责传感/电机闭环实验，Runtime 仓负责 Linux 边缘调度与 CAN fd——**相关、分层、不重复 PID**。

---

## 3. 30 秒（本仓主线）

> 前序项目已经能做设备控制和 ROS 2 任务。这个仓库补长期运行缺的 Linux Runtime：设备监督、命令 session/sequence/deadline、watchdog、故障恢复和 epoll/SocketCAN 生命周期。物理演示在 Orange Pi 上跑 `rcr_cell_app`：SG90 运动经 PA0 光电变成 CAN `POSITION_REACHED`，应用层 `CellReady` 再确认 MR0 DO0。Qt 只是工程站，不拥有 CAN 或闭环。不是硬实时，也不是功能安全。

---

## 3.1 3–5 分钟

1. **背景**：能动 ≠ 能长期跑。本仓做 Edge Runtime，不重复 PID。
2. **架构**：Qt `--cell-peer` → CEL1 → `rcr_cell_app`（`RuntimeDaemon` + `CellReadyMapper`）→ CAN/STM32 与 localhost Modbus agent → MR0。`rcrd` 是同一 daemon 的独立 host。
3. **Runtime**：状态机、admission、lease、CommLoss；离开 Active 不自动重放旧命令。
4. **STM32**：PA8 两档 SG90，PA0 去抖到位，不拥有 CellReady。
5. **Cell**：CellReady 是应用策略；agent 只回答怎么访问物理 I/O；`requested != confirmed`。
6. **故障**：CAN loss ≠ RS-485 loss。后者 Qt 仍可用，Probe 后不重放 DO0。
7. **限制**：SG90/光电/项目 CAN；软件过测 ≠ 闭环 13 项实物 PASS。

调度 OTHER vs FIFO 的 2 分钟测量案例仍可用（下一节），不要和闭环 Demo 混成一个 PASS。

---

## 4. 2 分钟主案例：OTHER vs FIFO

1. **Situation**：边缘周期监督线程在 1 ms 量级跑空 callback，需要知道抖动主要来自调度还是业务。  
2. **Task**：在 Orange Pi 普通内核上，用同一套合同对比 affinity、governor、OTHER/FIFO、同核/异核压力。  
3. **Action**：  
   - 固定 A76/`cpu7`、performance；  
   - OTHER+同核 stress：miss 到约 4e4 量级，p99/max 恶化到毫秒级；  
   - FIFO 同条件：miss 0，尾延迟回到数十微秒量级；  
   - 再用 cyclictest 四代表格核对方向；  
   - 另用 RT3 夹具看 mlock/PI/周期内分配等用户态抖动源。  
4. **Result**：建立「同核 OTHER 争用是当前最强可重复恶化源；FIFO 改善明显但 ≠ 硬实时」的叙述；PREEMPT_RT 因 Gate Blocked 未装，不编造 RT 核收益。  
5. **边界**：dirty smoke；空 callback ≠ CAN e2e；数字以 `evidence/portfolio/` 摘要为准。

---

## 5. 高频追问速答（底层岗）

| 问题 | 答法要点 |
|---|---|
| FIFO 是硬实时吗？ | 不是。是调度策略；最坏时延还取决于内核、IRQ、业务路径。 |
| 为什么还要绝对睡眠？ | 消累计漂移；不消 jitter。 |
| epoll 为什么重要？ | 一线程等多 fd；有真实 I/O 才进 Runtime，不空转。 |
| mailbox 为什么 latest-wins？ | 设定点可覆盖；故障/边沿不能静默覆盖。 |
| 板上跑起来了吗？ | 构建/安装/调度矩阵有。stock 无 CAN。can1 跑过 `vcan0+rcrd`。can2/STM32 有双向 CAN 与 SG90 目视 smoke。作品集主 Demo 是 `rcr_cell_app`，不要与 `rcrd` 同时写 can0。闭环 13 项未入库前是 NOT RUN。 |
| 和机器人有什么关系？ | 三层相关工程：Runtime（Linux）+ 控制/总线面 + MCU 面；分层证明周期与失败语义，不是附录彩蛋。 |
| 自动化转行缺什么？ | 用测量补课：亲和性、压力、PI、分段时延；用禁区清单防夸大。 |

---

## 6. 禁止话术

| 不要说 | 改说 |
|---|---|
| 已实现硬实时 | 普通内核上测量了 FIFO/OTHER 差异 |
| 已上 PREEMPT_RT | RT4 Gate Blocked，未安装 |
| Orange Pi 上 rcrd 已稳定常驻 | stock 默认未 active；can1 跑过 vcan 软件链，不是冷启动常驻 |
| 物理 CAN 已完整验收 | can2/STM32 已有 dirty-tree 双向 CAN、PC13、SG90 双位置目视动作和仲裁诊断；波形、完整故障矩阵、`rcrd`/Qt physical 未跑 |
| 空 callback 延迟 = 控制端到端 | 只反映唤醒 lateness |
| 软件 EStop = 功能安全 | 软件行为演示，无物理安全回路 |
| Modbus 双机 = 工业现场集成 | Wi-Fi demo，非 Runtime 主证据 |

---

## 7. 投递时附件建议

1. [PORTFOLIO_SUMMARY.md](../PORTFOLIO_SUMMARY.md)（1–2 页主入口）  
2. 本页口述 + GitHub README  
3. 四件套：架构图、Qt Overview 截图、Demo 录像、故障一段（采集后）  
4. 调度图仍可用 `evidence/portfolio/figures/`，勿与闭环 PASS 混写
