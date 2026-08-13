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

- 设计并实现 C++20 ROS-free Runtime：`CLOCK_MONOTONIC` 绝对周期睡眠、可选 `SCHED_FIFO`（失败显式降级）、`epoll` 反应器、SocketCAN、watchdog、状态机与固定容量 trace。  
- 普通输出采用 latest-wins mailbox；命令强制 session、严格递增 sequence 与 deadline，恢复后不自动重放；模拟节点用同一 deadline 作为普通输出 lease，到期归零。

- ThinkPad/`vcan` 路径验证节点模拟、双进程验收与故障矩阵；Orange Pi 4 Pro 完成 aarch64 原生构建、release/systemd 安装合同。  
- 在普通内核上建立调度对照：同核 `SCHED_OTHER` 压力下 wakeup miss 显著恶化，同条件 `SCHED_FIFO` miss 降为 0（60s smoke，空 callback）；并用 cyclictest 做方向一致性核对。  
- 明确边界：厂商镜像未启用 SocketCAN 故板上 daemon 未常驻；未安装 PREEMPT_RT；不声称硬实时或功能安全。

### 项目二（相关工程 · 控制 / 总线进程面）

- 臂遥操作栈中区分仿真与真机调度：仿真路径关闭 FIFO，避免高优先级控制线程与普通 DDS worker 形成优先级反转；稳态多速率分层（控制高于观测/策略合同频率）。  
- 虚拟 CANopen DS402 + `vcan` PDO/EMCY 路径，把「驱动状态机 / 心跳 / 故障帧」与本仓 SocketCAN Runtime 面放在同一叙事里讲清分层，而不是写成两个无关 Demo。

### 项目三（相关工程 · MCU 传感执行面）

- STM32 FreeRTOS 姿态与状态判别、ESP32 micro-ROS 桥、N20 编码器电机 PI 速度环台架：证明周期任务与线级合同在 MCU 侧同样成立。  
- 与本仓分工明确：MCU 仓负责传感/电机闭环实验，Runtime 仓负责 Linux 边缘调度与 CAN fd——**相关、分层、不重复 PID**。

---

## 3. 30 秒自我介绍

> 我是自动化转系统软件。主工程是 ROS-free 的 C++ Linux 边缘 Runtime：绝对时间周期线程、FIFO 可申请可降级、epoll 驱动的 SocketCAN，以及带 session/序号/截止时间的命令合同。同一叙事下还有两层相关工程：臂控制栈里处理多速率和仿真关 FIFO，MCU 侧做传感桥与电机 bench。它们不是一个合并产品，而是用户态 Runtime、控制进程面、单片机面各自把周期和失败语义做清楚。ARM 上我能量化同核 OTHER 会打穿周期、FIFO 能明显降 miss，同时说明这是普通内核测量，不是硬实时。

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
| 板上跑起来了吗？ | 构建/安装/调度矩阵有。stock 默认无 CAN、非冷启动常驻；can1 跑过 `vcan0 + rcrd`。独立 can2/STM32 有物理 smoke，但没跑 `rcrd`、不是 B4。 |
| 和机器人有什么关系？ | 三层相关工程：Runtime（Linux）+ 控制/总线面 + MCU 面；分层证明周期与失败语义，不是附录彩蛋。 |
| 自动化转行缺什么？ | 用测量补课：亲和性、压力、PI、分段时延；用禁区清单防夸大。 |

---

## 6. 禁止话术

| 不要说 | 改说 |
|---|---|
| 已实现硬实时 | 普通内核上测量了 FIFO/OTHER 差异 |
| 已上 PREEMPT_RT | RT4 Gate Blocked，未安装 |
| Orange Pi 上 rcrd 已稳定常驻 | stock 默认未 active；can1 跑过 vcan 软件链，不是冷启动常驻 |
| 物理 CAN 已完整验收 | can2/STM32 只有 dirty 双向、SG90 目视和仲裁 smoke；波形、完整故障矩阵、`rcrd`/Qt physical 未跑 |
| 空 callback 延迟 = 控制端到端 | 只反映唤醒 lateness |
| 软件 EStop = 功能安全 | 软件行为演示，无物理安全回路 |
| Modbus 双机 = 工业现场集成 | Wi-Fi demo，非 Runtime 主证据 |

---

## 7. 投递时附件建议

1. 本页 + [SYSTEMS_SOFTWARE_PORTFOLIO.md](SYSTEMS_SOFTWARE_PORTFOLIO.md)（或导出 PDF）  
2. `evidence/portfolio/figures/` 四图  
3. 可选：`orangepi_rt7_wrapup_20260805.md` 一页收口  
4. GitHub：本仓 README + `docs/portfolio/`（数字与 dirty 标签保持一致）
