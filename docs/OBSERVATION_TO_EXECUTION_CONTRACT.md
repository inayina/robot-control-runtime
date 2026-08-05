# 观测 → 执行接点合同（未实现 · Deferred）

状态：**Frozen as boundary only** — 只冻结职责与字段契约，**不**实现模块、不修改 `rcrd`。  
关联：[`experiments/multibus_observer/`](../experiments/multibus_observer/README.md)、
[`docs/KNOWLEDGE_BASE.md`](KNOWLEDGE_BASE.md) §6.13、[`docs/LINUX_RUNTIME.md`](LINUX_RUNTIME.md)、
[`docs/ARCHITECTURE.md`](ARCHITECTURE.md) §5

## 0. 为什么单独写这份合同

仓库里已经有两段**可分开验证**的设计：

| 段 | 现状 | 入口 |
|---|---|---|
| 观测段 | 已实现（实验） | `ObservationStore`：多源、单调时间、分源健康、stale |
| 执行段 | 已实现（Runtime） | `OutputCommand`：session / sequence / deadline；单笔在途 `OutputStatus` 匹配；命令/ACK timeout → Hold |

两端思路对齐（时效、不得静默重放、慢 I/O 不进周期线程），但**没有**把观测快照焊进
命令下发的产品链路。本文固定「若将来要接，接点长什么样、谁不能越权」，避免面试或后续
实现时把两段说成已经端到端打通，或贸然抽 `IBus` / 把融合塞进 `PeriodicScheduler`。

**何时才实现接点**：出现真实第二消费者（例如限速/Hold 策略，而不只是终端打印），且
V1 / Orange Pi / 协议实验主线不再抢优先级。未满足前保持 Deferred。

## 1. 分层图（目标形态，非当前进程拓扑）

```text
[观测段 · 可独立进程]
  CAN / Modbus /（未来其它慢源）
       → 各源自有线程与失败语义
       → ObservationStore（只读快照：值 + ts + source health + stale）
       ✗ 不下发 CAN 命令  ✗ 不改 Runtime 状态机

              ┊  接点（本文 · 未实现）
              ┊  Application / Adapter 进程内（禁止进周期 callback）
              ┊  Observation → Intent/Constraint → ExecutionGate
              ▼

[执行段 · rcrd]
  ExecutionGate 通过后 → OutputCommand（session / sequence / deadline_ns）
       → CommandMailbox → CanIoLoop 编码发送
       → 过期拒绝；匹配 APPLIED 才确认；watchdog/ACK 超时 → Hold + 清输出
```

当前仓库停在「观测段实验」与「执行段 Runtime」两盒；中间竖虚线**尚未编码**。

## 2. 职责边界

| 角色 | 负责 | 明确禁止 |
|---|---|---|
| 观测段 | 采集、解码、设备语义映射、分源 `healthy/faulted`、样本年龄与 stale | 调用 `publish_output_command`；驱动 Boot/Activate；冒充硬件急停 |
| 接点 / Gate（未来） | 读快照；按合同产出**约束或意图**；映射为带截止时间的命令或明确「不下发」 | 在 Gate 内做 socket/TCP；持有 SocketCAN fd；实现通用插件总线 |
| 执行段（已有） | session/序号/deadline 门控、mailbox、I/O 发送、命令 watchdog、状态机 | 在 `on_tick` 里跑 Modbus/视觉/融合；信任无 deadline 的“最新 float” |

**锁与线程**：接点若实现，必须在 **Application/Adapter 线程或独立低频进程**；  
**不得**进入 `PeriodicScheduler` callback（知识库 §6.13 已说明：慢超时会拖垮监督）。

## 3. 观测侧出口合同（已有实验应对齐的语义）

实现接点时应消费与 `ObservationStore` 同级的信息，而不是裸寄存器：

| 字段语义 | 要求 |
|---|---|
| 样本值 | 强类型；设备合同（如 `0.1°C`）写在适配层，不靠运行时字符串键 |
| `monotonic_ns` / age | 与 Runtime 命令相同时钟域偏好：`CLOCK_MONOTONIC`；跨主机不共享该时钟 |
| source health | 至少区分 healthy / faulted（可扩展 offline） |
| stale | 由年龄相对阈值门限得出；stale 样本不得假装“当前真值” |

辅助源（如温度）faulted：**不得**自动等价于 Runtime `HOLD` 或功能安全停机；是否限速/忽略由
接点策略显式写出，并留下可测行为。

## 4. 执行侧入口合同（已有 Runtime，接点必须迁就）

接点产出若要影响节点输出，最终必须落到现有：

```text
OutputCommand {
  session_id, sequence, deadline_ns, mask, values, ...
}
```

| 规则 | 说明 |
|---|---|
| 必须带 `deadline_ns` | 禁止“融合结果常驻、直到被替换”的隐式永久命令 |
| 必须服从 session / sequence | 恢复后不得重放接点里缓存的旧意图 |
| 过期由执行段拒绝 | Gate 也可提前因 stale 选择不下发；两层都可否决 |
| mailbox 语义 | 仅普通可覆盖输出；故障边沿仍走既有事件/状态路径，不塞进 latest-wins |

线级有效期与内部绝对 deadline 的换算仍按 CAN V1 / Runtime 现有合同，接点不另发明一套。

## 5. 策略草表（未来实现时填实，现仅占位）

以下**不是**已实现行为，只防止日后随意拍脑袋：

| 观测条件（示例） | 接点倾向 | 执行段已有后盾 |
|---|---|---|
| 全源 healthy 且未 stale | 可生成带短 deadline 的普通输出意图 | deadline / watchdog |
| 辅助源 faulted 或 stale | 默认：不下发依赖该源的意图；或显式限幅策略（须单测） | 不自动 HOLD |
| 控制相关源（若未来定义）faulted | 显式：Hold 请求或禁止 Activate 条件（须设计评审） | `handle(Fault*)` / Hold |
| 融合/策略线程卡住 | 不再刷新命令 → 执行段 command timeout → Hold | watchdog |

「控制相关源」今日**未**在 multibus 实验中定义；在定义前，温度类源一律按辅助源处理。

## 6. 与 ROS 2 Adapter 的关系

- ROS 2 Adapter（路线图阶段 8）：Topic/API ↔ Runtime，**低频**，不侵入 Core。  
- 本接点：更偏「多总线观测 → 约束/命令」，可与 Adapter **并列**为 Application 外侧组件。  
- **不要求**先做完本接点才能做 ROS；也**不**把本接点做成第二个通用中间件。

## 7. 明确不在本合同范围

- 抽统一 `IBus::read_all_signals()` 或字符串信号表；  
- Kalman/EKF 等估计算法（放姐妹仓或独立实验）；  
- 宣称软件路径 = 功能安全或硬实时；  
- 把 localhost / `vcan` 观测演示写成现场多传感器融合闭环已验收。

## 8. 实现前 Gate（将来编码用）

1. 书面策略表（§5）填完并通过评审；  
2. 单测：stale / 辅助 fault → 不下发或限幅可重复；  
3. 证明 Gate 线程不做 socket；周期线程延迟不因 Gate 增加；  
4. 仍通过既有 `rcrd` 故障矩阵中与 deadline / session 相关场景；  
5. 文档与知识库同步改为“实验使用过”或“板上测过”，删除 Deferred 夸大表述。

## 9. 证据等级（当前）

| 陈述 | 等级 |
|---|---|
| 观测段多源快照与 stale | 代码中使用过（实验） |
| 执行段 deadline / session / watchdog | 代码中使用过（Runtime + 故障矩阵） |
| 观测→执行完整链路 | **未实现**；仅有本文边界合同 |
