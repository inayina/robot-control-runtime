# CAN Runtime V1 线级合同

状态：**Frozen（2026-08-10 重新冻结；补入普通输出 lease，见 §11）**

版本号：`protocol_version = 1`  
适用范围：Linux Runtime ↔ 独立 CAN 节点模拟器（`vcan0`）；未来物理 CAN 复用同一合同。

本文是唯一权威线级说明。不引用 C++ 对齐、主机字节序或 `rcr::OutputCommand` 内存布局。
Linux 内部类型与本文件字段同名不等于可 `memcpy`。

---

## 1. 参与者与方向

| 角色 | 进程 | 职责 |
|---|---|---|
| Runtime | Linux daemon / 验收进程 | 监督 heartbeat、下发 OutputCommand、观察 Status |
| Node | `rcr_node_sim`（独立进程） | 发 Heartbeat/Status/OutputStatus；按合同接受或拒绝命令 |

Fault Injection **不是**正式消息：仅由模拟器启动参数启用，默认关闭。

---

## 2. 物理与帧层约束

| 项 | 合同 |
|---|---|
| 帧格式 | 经典 CAN 2.0A，**标准 11-bit ID** |
| 数据长度 | 四类消息 **DLC 必须为 8** |
| 字节序 | 所有宽度 > 8 bit 的整数均为 **大端（网络序）** |
| 扩展帧 (IDE/EFF) | 拒绝 |
| RTR | 拒绝 |
| 错误帧 | 不进入本 codec；由 SocketCAN/驱动路径单独统计 |
| 填充位 | 由控制器处理；合同不依赖位填充计数 |

接收端解码前必须检查：标准数据帧、DLC=8、ID 落在已分配功能窗。任一失败 →
丢弃并计 `ProtocolReject`（不抛异常越过 fd 循环）。

---

## 3. CAN ID 分配

11-bit ID 布局：

```text
 bits 10..5  function  (6 bit)
 bits  4..0  node_id   (5 bit)
```

```text
can_id = (function << 5) | node_id
```

| function | 值 | 消息 | 方向 | ID 范围（node 1..31） |
|---|---|---|---|---|
| `FN_HEARTBEAT` | `0x01` | NodeHeartbeat | Node → Runtime | `0x021` .. `0x03F` |
| `FN_STATUS` | `0x02` | NodeStatus | Node → Runtime | `0x041` .. `0x05F` |
| `FN_OUT_CMD` | `0x03` | OutputCommand | Runtime → Node | `0x061` .. `0x07F` |
| `FN_OUT_STAT` | `0x04` | OutputStatus | Node → Runtime | `0x081` .. `0x09F` |

### 节点范围

| `node_id` | 含义 |
|---|---|
| `0` | 非法；编解码均拒绝 |
| `1` .. `31` | 合法节点 |
| `> 31` | 非法（5-bit 字段不可能出现；若原始 ID 使 function 未知则整帧拒绝） |

V1 验收默认节点：`node_id = 1` → Heartbeat `0x021`，Status `0x041`，
Command `0x061`，OutputStatus `0x081`。

### 优先级

CAN 仲裁下 ID 越小优先级越高。本分配使 Heartbeat > Status > Command > OutputStatus，
保证存活信号优先于普通输出反馈。

### 未知 function

`function` 不是上表四值 → 整帧拒绝（即使 DLC=8）。

---

## 4. 公共字段规则

### 4.1 `protocol_version`（每帧 byte0）

| 值 | 行为 |
|---|---|
| `1` | 本文件合同 |
| 其他 | 拒绝 |

### 4.2 保留位 / 保留字节

凡标注 reserved 的 bit 或 byte，发送必须为 0；接收发现非 0 → 拒绝。

### 4.3 `session_id`（u16）

| 规则 | 说明 |
|---|---|
| 生成方 | **Node** 在每次进程启动（含故障注入“重启”）时生成 |
| 取值 | `1 .. 65535`；**禁止 0** |
| 变化 | 仅在 Node 启动/重启时变化；heartbeat 周期内保持不变 |
| Runtime | 从 Heartbeat/Status 学习当前 session；OutputCommand 必须携带该值 |
| 旧命令 | Node 收到 `session_id != 当前 session` 的命令 → 拒绝，不改输出 |

`boot_id` 与 `session_id` 独立计数：允许相等（模拟器可令二者相同以便复现），
但接收端不得假设相等。

### 4.4 `boot_id`（u16，仅 Heartbeat）

| 规则 | 说明 |
|---|---|
| 生成方 | Node，每次进程启动递增 |
| 取值 | `1 .. 65535`；禁止 0；从上次值 `+1`，`65535` 之后回到 `1` |
| 用途 | Runtime 观测“节点经历了一次新启动”；与 session 同时变化时应进入重绑定路径 |

### 4.5 u16 序号回绕比较

对 `hb_seq`、`sequence`、`applied_sequence` 使用十六位序号算术（与 RFC 1982 同形）：

```text
newer(a, b) := (a ≠ b) ∧ (((a - b) mod 65536) < 32768)
```

等价实现（有符号 16 位环差）：

```text
newer(a, b) := (a ≠ b) ∧ (int16_t)(a - b) > 0
```

| 场景 | 行为 |
|---|---|
| OutputCommand `sequence` | 必须对上次**已接受**序号满足 `newer(seq, last)`；且 `seq ≠ 0` |
| 重复或倒退 | 拒绝；不更新输出；OutputStatus 报告对应原因 |
| `hb_seq` | 允许丢帧造成的间隙；仅用于观测，不因间隙拒绝后续 heartbeat |
| 回绕 | `65535` 之后下一个合法新序号为 `0`（heartbeat）或对 command 为满足 `newer` 的下一值；command 仍禁止把 `0` 当作**首个**序号——见下 |

OutputCommand 额外约束：`sequence == 0` **永远非法**（与 Runtime 内部“非零序号”对齐）。
因此 command 序号空间为 `1..65535`，在 `65535` 之后下一个可接受值需满足
`newer(seq, 65535)` 且 `seq ≠ 0` → 即 `1`（因为 `(1-65535)` 的 int16 差为正）。

### 4.6 相对有效期 → 本地 deadline

线上**禁止**传输绝对 `CLOCK_MONOTONIC` 时间戳（Linux 与未来 MCU 无共享单调钟）。

OutputCommand 携带 `validity_10ms`（u8）：

| 值 | 含义 |
|---|---|
| `0` | 非法 |
| `1 .. 250` | 相对有效期 = `value × 10 ms` → `10 ms .. 2500 ms` |
| `251 .. 255` | 非法 |

接收端（Node）在成功通过帧层与会话校验后：

```text
deadline_local_ns = receive_time_monotonic_ns + validity_10ms × 10 × 1_000_000
```

若在应用输出前 `now_ns >= deadline_local_ns` → 视为过期拒绝。

命令成功 `APPLIED` 后，同一个 `deadline_local_ns` 继续作为该普通输出的本地
**lease（租约）截止点**：它表示 Runtime 对这份普通输出最多拥有到何时，而不只表示
“队列里的命令来不来得及执行”。Node 必须遵守：

1. `now_ns < deadline_local_ns` 时，输出可保持或被更新序号的新命令替换；
2. `now_ns >= deadline_local_ns` 时，普通输出进入中性值（V1 八路输出为 `0`）；
3. 只有成功 `APPLIED` 的新命令刷新 lease；任何拒绝都不得延长旧输出；
4. `interlock_ready: true → false` 或 soft restart 立即使输出归零并取消 lease；
5. lease 到期不清已接受序号，恢复后仍须当前 session 的更新序号命令，不能重放旧目标。

该行为是软件普通输出的 fail-neutral 合同，不是功能安全、硬件急停或 STO。Node 使用自己的
单调时钟执行，不要求 Linux 与 MCU 时钟同步。

Runtime 从内部绝对 `deadline_ns` 编码时：

```text
validity_10ms = ceil_clamp((deadline_ns - now_ns) / 10_000_000, 1, 250)
```

若内部剩余有效期 `< 10 ms` 或 `> 2500 ms` → **不得发送**（在 encode 侧失败），避免线上出现非法值。

---

## 5. 定时合同

| 参数 | 默认 | 范围 / 规则 |
|---|---|---|
| Heartbeat 周期 | `100 ms` | 模拟器可配；验收默认 100 |
| Heartbeat 超时 | `300 ms` | Runtime：距上次合法 Heartbeat ≥ 300 ms → CommLoss 路径 |
| NodeStatus 周期 | `100 ms` | V1 与 heartbeat 同频发送，降低状态撕裂窗口 |
| OutputStatus | 每条已解码的 OutputCommand 对应一条响应 | 接受或拒绝都要响应（非法到无法解析 function/DLC 的帧除外） |
| 普通输出 lease | 每条命令的 `validity_10ms` | Applied 后至本地 deadline；到期/联锁丢失/重启归零 |

总线负载粗算（假设 500 kbit/s、每帧约 130 bit 含填充上限）：

| 流量 | 估算 |
|---|---|
| HB 10 Hz + Status 10 Hz | ≈ 2.6 kbit/s |
| 命令/应答突发（10 Hz） | ≈ 2.6 kbit/s |
| 合计量级 | ≪ 5% @ 500 kbit/s |

vcan 不模拟位时序；该预算仅约束未来物理 CAN，防止 V1 消息膨胀。

---

## 6. 消息布局

以下偏移均为 payload 字节下标；多字节域为大端。

### 6.1 NodeHeartbeat — Node → Runtime

`can_id = (0x01 << 5) | node_id`，DLC=8。

| 偏移 | 宽度 | 字段 | 规则 |
|---|---|---|---|
| 0 | u8 | `protocol_version` | 必须为 `1` |
| 1 | u8 | `flags` | 全 0；非 0 拒绝 |
| 2..3 | u16 BE | `boot_id` | `1..65535` |
| 4..5 | u16 BE | `session_id` | `1..65535` |
| 6..7 | u16 BE | `hb_seq` | 每周期 +1，允许回绕到 0 |

谁生成：Node。  
何时变：`boot_id`/`session_id` 仅启动时；`hb_seq` 每周期。

### 6.2 NodeStatus — Node → Runtime

`can_id = (0x02 << 5) | node_id`，DLC=8。

| 偏移 | 宽度 | 字段 | 规则 |
|---|---|---|---|
| 0 | u8 | `protocol_version` | `1` |
| 1 | u8 | `flags` | bit0 = `interlock_ready`；bit1..7 必须 0 |
| 2..3 | u16 BE | `session_id` | 必须等于当前 Node session |
| 4..5 | u16 BE | `input_bits` | 演示用数字输入快照，任意 u16 |
| 6..7 | u16 BE | `fault_code` | 见 §7；未知码仍须投递，由 Runtime 解释 |

软件联锁与 fault 仅为学习模型，**不是**功能安全信号。

### 6.3 OutputCommand — Runtime → Node

`can_id = (0x03 << 5) | node_id`，DLC=8。

| 偏移 | 宽度 | 字段 | 规则 |
|---|---|---|---|
| 0 | u8 | `protocol_version` | `1` |
| 1 | u8 | `mask` | 非 0；位置位才更新对应输出 |
| 2..3 | u16 BE | `session_id` | 目标 Node 当前 session |
| 4..5 | u16 BE | `sequence` | `1..65535`，相对上次已接受序号须 `newer` |
| 6 | u8 | `values` | 仅 `mask` 置位的 bit 有意义 |
| 7 | u8 | `validity_10ms` | `1..250` |

应用规则（Node）：

1. 解码失败 → 无 OutputStatus，只计数拒绝；
2. `session_id` 不匹配 → OutputStatus `SESSION_MISMATCH`，输出不变；
3. `interlock_ready == false` → `NOT_READY`；普通输出已在联锁丢失时归零；
4. `sequence` 重复/倒退 → `STALE_SEQUENCE`；
5. 过期 → `EXPIRED`；
6. 否则应用 `output := (output & ~mask) | (values & mask)`，建立/刷新 §4.6 lease，
   OutputStatus `APPLIED`，`applied_sequence = sequence`。

### 6.4 OutputStatus — Node → Runtime

`can_id = (0x04 << 5) | node_id`，DLC=8。

| 偏移 | 宽度 | 字段 | 规则 |
|---|---|---|---|
| 0 | u8 | `protocol_version` | `1` |
| 1 | u8 | `result` | 见 §7.2；高 4 bit 必须 0 |
| 2..3 | u16 BE | `session_id` | Node **当前** session（拒绝时也报当前值） |
| 4..5 | u16 BE | `sequence` | 被接受或被拒绝的命令序号 |
| 6 | u8 | `output_mirror` | 当前全部输出位镜像 |
| 7 | u8 | `reserved` | 必须 0 |

---

## 7. 枚举

### 7.1 `fault_code`（NodeStatus，u16 BE）

与学习用 Fault 语义对齐的数值；未列出的值 Runtime 视为 `UNKNOWN` 但仍接受帧：

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `NONE` | 无故障 |
| 1 | `WATCHDOG` | 节点侧监督超时（演示） |
| 2 | `INPUT_FAULT` | 输入路径异常（演示） |
| 3 | `COMM_LOSS` | 节点认为通信异常（演示） |
| 4 | `NODE_FAULT` | 节点内部故障 |
| 5 | `PROTOCOL_REJECT` | 累计协议拒绝（可选上报） |
| 6 | `INTERLOCK_LOST` | 软件联锁丢失 |
| 7 | `INTERNAL` | 其他内部错误 |

### 7.2 `result`（OutputStatus，u8 低 4 bit）

| 值 | 名称 | 何时 |
|---|---|---|
| 0 | `APPLIED` | 输出已按 mask 更新 |
| 1 | `STALE_SEQUENCE` | 重复或倒退序号 |
| 2 | `SESSION_MISMATCH` | 命令 session ≠ 当前 |
| 3 | `EXPIRED` | 超过相对有效期 |
| 4 | `INVALID_MASK` | `mask == 0`（若漏过 encode） |
| 5 | `NOT_READY` | 节点未就绪（联锁未满足等，演示） |
| 6..15 | 保留 | 发送禁止；接收视为非法帧 |

---

## 8. 拒绝行为总表

| 条件 | 解码 | OutputStatus | 输出 |
|---|---|---|---|
| EFF / RTR | 拒绝 | 无 | 不变 |
| DLC ≠ 8 | 拒绝 | 无 | 不变 |
| 未知 function / node_id=0 | 拒绝 | 无 | 不变 |
| `protocol_version ≠ 1` | 拒绝 | 无 | 不变 |
| 保留位/字节非 0 | 拒绝 | 无 | 不变 |
| Heartbeat `boot_id`/`session_id` = 0 | 拒绝 | 无 | — |
| Command `sequence`/`mask`/`validity` 非法 | 拒绝 | 无* | 不变 |
| Command session 不匹配 | 接受为已解码命令 | `SESSION_MISMATCH` | 不变 |
| Command 序号陈旧 | 已解码 | `STALE_SEQUENCE` | 不变 |
| Command 过期 | 已解码 | `EXPIRED` | 不变 |

\*encode 侧应阻止非法 command 上总线；若仍收到，按帧层拒绝（无 OutputStatus）。

---

## 9. 与 Runtime 内部类型的边界

| 内部（`rcr::OutputCommand`） | 线级 OutputCommand |
|---|---|
| `session_id` u64 | u16 **Node** session |
| `sequence` u64 | u16，回绕比较 |
| `deadline_ns` 绝对单调时 | `validity_10ms` 相对 |
| `mask`/`values` u32 | u8（V1 仅 8 路演示输出） |

编解码器（P2）负责显式转换；禁止把内部 struct 映像到 CAN data。

---

## 10. Golden vectors

下列向量冻结后，P2 codec 测试必须字节级一致。  
`data` 为 8 字节十六进制；`can_id` 为 11-bit 数值（不含 Linux EFF/RTR 标志位）。

默认节点：`node_id = 1`。

### 10.1 NodeHeartbeat

| 名称 | can_id | data | 期望 |
|---|---|---|---|
| `hb_min` | `0x021` | `01 00 00 01 00 01 00 00` | OK：ver=1, boot=1, session=1, hb_seq=0 |
| `hb_typical` | `0x021` | `01 00 00 02 00 0A 01 00` | OK：boot=2, session=10, hb_seq=256 |
| `hb_wrap_seq` | `0x021` | `01 00 00 01 00 01 FF FF` | OK：hb_seq=65535 |
| `hb_bad_version` | `0x021` | `02 00 00 01 00 01 00 01` | REJECT：version=2 |
| `hb_bad_flags` | `0x021` | `01 01 00 01 00 01 00 01` | REJECT：flags≠0 |
| `hb_zero_session` | `0x021` | `01 00 00 01 00 00 00 01` | REJECT：session=0 |
| `hb_zero_boot` | `0x021` | `01 00 00 00 00 01 00 01` | REJECT：boot=0 |
| `hb_rtr` | `0x021` + RTR | （忽略 data） | REJECT：RTR |
| `hb_ext` | EFF + id | （忽略 data） | REJECT：扩展帧 |
| `hb_dlc7` | `0x021` | 7 字节任意 | REJECT：DLC≠8 |

### 10.2 NodeStatus

| 名称 | can_id | data | 期望 |
|---|---|---|---|
| `st_min` | `0x041` | `01 00 00 01 00 00 00 00` | OK：interlock=0, inputs=0, fault=NONE |
| `st_typical` | `0x041` | `01 01 00 0A 00 03 00 06` | OK：interlock=1, inputs=3, fault=INTERLOCK_LOST |
| `st_inputs_max` | `0x041` | `01 01 00 01 FF FF 00 00` | OK：inputs=0xFFFF |
| `st_bad_reserved_flag` | `0x041` | `01 02 00 01 00 00 00 00` | REJECT：bit1 set |
| `st_zero_session` | `0x041` | `01 01 00 00 00 00 00 00` | REJECT |

### 10.3 OutputCommand

| 名称 | can_id | data | 期望 |
|---|---|---|---|
| `cmd_min` | `0x061` | `01 01 00 01 00 01 01 01` | OK：mask=1, session=1, seq=1, values=1, valid=10ms |
| `cmd_typical` | `0x061` | `01 0F 00 0A 00 05 05 0A` | OK：mask=0x0F, session=10, seq=5, values=5, valid=100ms |
| `cmd_validity_max` | `0x061` | `01 01 00 01 00 02 01 FA` | OK：validity_10ms=250 → 2500ms |
| `cmd_seq_prewrap` | `0x061` | `01 01 00 01 FF FF 01 0A` | OK：seq=65535（若 last 使 newer 成立） |
| `cmd_zero_mask` | `0x061` | `01 00 00 01 00 01 01 0A` | REJECT |
| `cmd_zero_seq` | `0x061` | `01 01 00 01 00 00 01 0A` | REJECT |
| `cmd_zero_session` | `0x061` | `01 01 00 00 00 01 01 0A` | REJECT |
| `cmd_validity_0` | `0x061` | `01 01 00 01 00 01 01 00` | REJECT |
| `cmd_validity_251` | `0x061` | `01 01 00 01 00 01 01 FB` | REJECT |

语义例（不单测字节，供模拟器场景）：若 last_accepted=5，则 `seq=5` → STALE；`seq=4` → STALE；`seq=6` → 可接受。

### 10.4 OutputStatus

| 名称 | can_id | data | 期望 |
|---|---|---|---|
| `os_applied` | `0x081` | `01 00 00 0A 00 05 05 00` | OK：APPLIED, session=10, seq=5, mirror=0x05 |
| `os_stale` | `0x081` | `01 01 00 0A 00 05 04 00` | OK：STALE_SEQUENCE, mirror 未变示例 |
| `os_session` | `0x081` | `01 02 00 0B 00 05 00 00` | OK：SESSION_MISMATCH |
| `os_expired` | `0x081` | `01 03 00 0A 00 05 00 00` | OK：EXPIRED |
| `os_bad_result` | `0x081` | `01 06 00 0A 00 05 00 00` | REJECT：result=6 保留 |
| `os_bad_reserved` | `0x081` | `01 00 00 0A 00 05 00 01` | REJECT：byte7≠0 |
| `os_high_nibble` | `0x081` | `01 10 00 0A 00 05 00 00` | REJECT：result 高位非 0 |

### 10.5 ID / 节点边界

| 名称 | can_id | 说明 |
|---|---|---|
| `id_node0_hb` | `0x020` | function=HB, node=0 → REJECT |
| `id_unknown_fn` | `0x0A1` | function=0x05, node=1 → REJECT |
| `id_node31_hb` | `0x03F` | node=31 合法窗上界；payload 仍须满足 Heartbeat 规则 |

---

## 11. 变更规则

- 2026-08-10 在尚无物理 endpoint/固件实现的阶段显式修订并重新冻结 V1：字节布局、
  `protocol_version` 和 golden vectors 均未改变；§4.6 明确 `validity_10ms` 在 Applied 后
  继续约束普通输出 lease。旧的 2026-08-01 文本只定义应用前 expiry，现由本版取代。
- 本文件 `protocol_version = 1` 字段布局冻结后，只允许文档勘误，不得静默改字节含义。
- 不兼容变更必须升高 `protocol_version`，并同时提供新的 golden vectors。
- endpoint 行为修订也必须先改本合同、模拟器与黑盒验收；不得只改某个节点实现。
- Fault Injection 参数不得占用上表 function 号。

---

## 12. 实现顺序提示（非合同正文）

1. P2：无状态 encode/decode + 上表向量单测；
2. P3：`rcr_node_sim` 按本节定时与拒绝表行为实现；
3. P4：双进程只经 `vcan0` 跑正常 / 重启 / 乱序 / 超时 / 非法帧。
