# EtherCAT 协议笔记（零成本预习）

状态：Living（**理解过** · 本仓尚未联调 SubDevice；ThinkPad NIC Gate 见
`docs/ETHERCAT_NIC_GATE.md`）  
依据：

- [ETG EtherCAT Technology](https://www.ethercat.org/en/technology.html)（公开技术概述）
- [ETG EtherCAT Compendium](https://www.ethercat.org/en/compendium.htm)（完整 PDF 需成员登录；章节持续发布）
- 路线图：`docs/plans/DEVELOPMENT_ROADMAP.md` §8
- 知识库预习卡：`docs/KNOWLEDGE_BASE.md` §6.10 / §6.12
- NIC 前置 Gate：`docs/ETHERCAT_NIC_GATE.md`

本文件把“读完公开资料后应能口述的模型”钉下来。它**不是**实现合同，也不是
周期/安全证据。IEC 61158 细节与厂商 ESI 以正式规范与设备手册为准。

## 给初学者的一句话

EtherCAT 是**一帧过程映像、从站硬件飞过读写**的现场总线：只有主站主动发帧，从站的
**ESC**（EtherCAT Slave Controller）在报文经过时插入/取走自己的字节，报文再折返回主站。
它不是“把 TCP 跑快一点”，也不是每个从站各开一条 TCP 问答应答。

## 怎样读 EtherCAT 预习材料

零采购阶段按这条链读，避免一上来扎进 SOEM 源码：

```text
① 本笔记（口述模型：帧 / ESC / FMMU·SM / INIT→OP / PDO·SDO / WKC）
     ↓
② docs/ETHERCAT_NIC_GATE.md（G1–G6：有线口能否当 SOEM 候选口）
     ↓
③ Gate 复跑脚本 + NM 独占（本机 host 证据；仍无从站）
     ↓
④ SOEM slaveinfo 空扫（ecx_init 打开适配器；No slaves found = 预期）
     ↓
⑤（有 SubDevice 后再）扫描 → 状态机 → PDO / WKC → 掉线恢复
```

| 材料 | 你应带走什么 | 证据等级 |
|---|---|---|
| 本笔记 | 能口述 on-the-fly、WKC、INIT→OP、PDO≠SDO | 理解过 |
| `ETHERCAT_NIC_GATE.md` | 为什么要测 AF_PACKET / Wi-Fi / NM；不能夸大成什么 | 快照可复跑（G1–G5） |
| `KNOWLEDGE_BASE.md` §6.10 / §6.12 | 面试口述 + 与 SocketCAN / Modbus 边界 | 理解过 |
| SOEM `slaveinfo`（无从站） | 适配器打开路径通了 | 工具链，≠ SubDevice PASS |
| 接上 I/O 从站后 | OP / PDO / WKC / 掉线 | **尚未开始**；不得提前写 PASS |

已做过 Modbus TCP 实验时：先用 `docs/MODBUS_TCP_NOTES.md` 巩固“问答应答 + 半包组帧”，
再读本笔记 §11 对照表——两种都叫“工业以太网通信”，层与合同完全不同。

## 1. 功能原理（on-the-fly）

```text
MainDevice（主站）发一帧
        ↓
  沿网线经过 SubDevice #1 → #2 → … → 末节点
        ↓
  末节点折返（逻辑环；物理上常是线型拓扑）
        ↓
  回到 MainDevice（同一帧完成整链读写）
```

- 每个从站的 **ESC** 在报文“飞过”时**硬件级**读写自己的数据；延迟主要是传播延迟，
  不是每个节点开一次 TCP 会话、等一次应用层应答。
- **只有 MainDevice 主动发帧**；从站转发/插入数据。这消除了多主争用带来的不可预期延迟。
- MainDevice 可用普通以太网 MAC + 用户态/内核主站栈；从站必须有 ESC（或集成 ESC 的模块）。
  普通 MCU + PHY **不能**只靠软件位拷贝冒充合格从站——来不及在线改帧。

直觉类比（帮助记忆，不是实现等价）：

| | 邮车沿路送信 | EtherCAT |
|---|---|---|
| 谁开车 | 一辆主站邮车 | 只有主站发帧 |
| 路边信箱 | 从站 ESC | 硬件在帧飞过时读写 |
| 回到邮局 | 报文折返主站 | 主站看到整链更新后的帧 |

面试一句话：EtherCAT 不是“把 TCP 跑快一点”，而是**一帧过程映像 + 硬件 on-the-fly**。

## 2. 帧结构（字节级直觉）

以太网帧使用 EtherType **`0x88A4`** 标识 EtherCAT（对抓包/Wireshark：`ethertype == 0x88a4`）。

```text
┌──────────────────┬────────────┬─────────────────────────────────────────┐
│ Ethernet header  │ EtherType  │ EtherCAT payload                        │
│ Dest/Src MAC …   │ 0x88A4     │ Header + 一个或多个 datagram            │
└──────────────────┴────────────┴─────────────────────────────────────────┘
                                         │
                    ┌────────────────────┴────────────────────┐
                    │ datagram #0                             │
                    │  ┌ header：命令、地址、长度 …            │
                    │  ┌ 数据区（留给从站读写的字节窗口）      │
                    │  └ WKC（Working Counter，2 字节量级口述）│
                    │ datagram #1 …                           │
                    └─────────────────────────────────────────┘
```

### 2.1 和 Modbus TCP 字节直觉的对照（先别混）

| | Modbus TCP（本仓已练） | EtherCAT（本笔记） |
|---|---|---|
| 你在看的“一笔” | 一个 ADU = MBAP + PDU | 一帧里可有多个 datagram |
| 定界 | TCP 流靠 MBAP.Length | 以太网帧边界 + EtherCAT 头长度字段 |
| 应答模型 | 一问一答，TransID 配对 | **同帧去返**；从站不另开 TCP 答 |
| 抓包工具 | `tcpdump` 看 TCP payload | 要能看到二层 `0x88A4`（且口常无 IP） |

预习阶段不必背完整 datagram 头每一位；先建立：**一帧可多段 datagram，每段有自己的寻址与 WKC**。

### 2.2 datagram 常见寻址方式（口述级别）

| 方式 | 用途直觉 | 典型阶段 |
|---|---|---|
| 位置 / 自动递增（position / auto-increment） | 按“第几个从站”扫拓扑 | 启动、与规划拓扑比对 |
| 配置站地址（configured station address） | 拓扑变化后仍可点名 | Hot Connect 等 |
| 逻辑寻址（logical） | 映射到整段**过程映像** | 周期 PDO：一帧更新多个从站 |

启动用位置寻址摸清“链上有谁”；配置完 FMMU/SM 后，周期数据走逻辑寻址过程映像——主站脑中是一块大缓冲，每个从站只“认领”其中几字节。

## 3. FMMU 与 SyncManager

| 组件 | 英文 | 职责 |
|---|---|---|
| 同步管理器 | SyncManager (SM) | 从站**本地缓冲**的门禁：方向、长度、邮箱 vs 过程数据、访问一致性 |
| 现场总线内存管理单元 | FMMU | 把主站**逻辑过程映像**中一段字节映射到本地地址（通常落到某 SM 缓冲） |

口头对照：

> SyncManager 管“这块本地内存怎么安全读写”；  
> FMMU 管“逻辑总线上的哪几个字节是我的”。

再具体一点：

```text
主站过程映像（逻辑字节 0..N）
        │  FMMU：映像偏移 → 某从站本地地址
        ▼
从站本地缓冲（由某 SyncManager 看守）
        │  SM：谁写谁读、多长、邮箱还是 PDO、防撕裂
        ▼
从站应用（读输入 / 写输出）
```

配置阶段（常经邮箱 / CoE SDO）写好 FMMU、SM、PDO mapping；进入周期运行后主站主要
填充/消费过程映像，而不是每周期重新发明映射。把 SDO 轮询塞进 1 ms 闭环是常见反模式。

## 4. 状态机：INIT → PREOP → SAFEOP → OP

| 状态 | 允许的大致行为（口述） |
|---|---|
| **INIT** | 初始化；邮箱通信通常不可用 |
| **PREOP** | Pre-Operational：可做**邮箱**配置（如 CoE/SDO）、设置映射 |
| **SAFEOP** | Safe-Operational：可交换输入过程数据；输出侧行为受限（以规范/设备为准） |
| **OP** | Operational：周期输入/输出过程数据 |

```text
INIT ──配置/邮箱──► PREOP ──映射就绪──► SAFEOP ──允许输出──► OP
  ▲                                                          │
  └──────── 故障 / 非法迁移 / AL 错误 → fail-closed ──────────┘
```

主站必须按允许迁移前进；非法迁移或 **AL status**（Application Layer 状态）错误应
fail-closed，而不是“socket 还能 write 就盲写输出”。

本仓约束：不把普通数字输出、软件停止或“进了 OP”描述为硬件安全功能。`SAFEOP` 名字里的
Safe **不是**本仓已验收的功能安全证据。

## 5. PDO vs SDO（职责分开）

| | PDO（过程数据） | SDO / 邮箱（配置与诊断） |
|---|---|---|
| 路径 | 周期过程映像，常经逻辑寻址 | Mailbox（非周期） |
| 典型用途 | 循环读输入、写输出 | 读写对象字典、配置 mapping、诊断 |
| 实时角色 | 闭环数据面 | 配置/监督面 |
| 本仓首轮 | 简单 I/O 的周期 PDO | 用邮箱完成 mapping；不把 SDO 塞进 1 ms 闭环 |

**CoE**（CAN application protocol over EtherCAT）是常用的邮箱应用层之一：用类似 CANopen
对象字典的方式做 SDO。首轮目标是“配置清楚 + 周期 PDO 正确”，不是实现全部 profile。

和 Modbus Holding 的粗对照（帮助记忆，协议不等价）：

- Modbus `0x03`/`0x06` 轮询 Holding ≈ “主站问寄存器”；可慢、可重试业务上要小心；
- EtherCAT PDO ≈ “每周期已经映好的那几字节跟着帧飞”；实时合同靠周期 + WKC，不是 TransID。

## 6. Working Counter 与 AL status

### Working Counter (WKC)

每个 datagram 带 WKC。被该 datagram **成功寻址且存储区可访问**的从站会自动递增 WKC。
主站在每周期核对：实际 WKC 是否等于期望值。

```text
期望 WKC = 本 datagram 设计上应成功参与的从站操作次数（口述：常与映射从站数相关）
实际 WKC ≠ 期望 → 掉线 / 状态不对 / 映射错 / 线缆问题
                → 不得把本周期数据当真并继续盲控
```

- WKC 符合预期：本 datagram 覆盖的从站数据可交给控制应用；
- WKC 不符：**fail-closed**——记录、降级、停输出策略以设备与本仓状态机为准，禁止静默当好帧。

### AL status / AL status code

Application Layer 状态寄存器反映从站应用层当前状态与错误码（例如请求进 OP 被拒绝的原因）。
主站在状态迁移与故障恢复时应读 AL status，而不是只看“`ecx_init` 成功”或“网卡还能 send”。

诊断还能结合链路状态、ESC 错误计数等定位故障区间（公开技术文强调错误可局域化）。

## 7. Distributed Clocks（先标边界）

DC（Distributed Clocks，分布式时钟）用硬件对齐采样/输出时刻；传播延迟可测并可补偿。
公开资料强调：即使主站发帧有数微秒级抖动，DC 驱动的本地动作仍可保持高精度——因此
**不能**用主站 `clock_nanosleep` 唤醒 jitter 冒充从站同步精度。

本仓顺序：先建立无 DC（或明确记录未启用 DC）的 PDO/WKC/掉线恢复基线，再另开 DC 实验。

## 8. 为什么需要 AF_PACKET（预习钩子）

从站 ESC 认的是链路层 EtherCAT 帧（`0x88A4`），不是“TCP 里塞点二进制”。用户态主站
（如 SOEM）典型经 Linux **`AF_PACKET` / raw** 在**专用网卡**上收发二层帧：

- 需要 `CAP_NET_RAW`（或 root）；
- 实验口应独占：勿与 DHCP/默认路由/日常上网抢同一物理口；
- 普通 loopback / `vcan` 思路**不能**冒充 ESC。

细节与本仓 Gate 检查见 `docs/ETHERCAT_NIC_GATE.md` 与 `KNOWLEDGE_BASE.md` §6.12。
**raw bind 通过 ≠ 周期确定性；更 ≠ SubDevice 已进 OP。**

## 9. 与本仓后续工作的衔接

| 本笔记 | 下一步零成本/低成本动作 |
|---|---|
| 帧 / `0x88A4` / raw | `ETHERCAT_NIC_GATE.md`：AF_PACKET、NIC 独占、G1–G6 |
| NM 抢走有线口 | `deploy/ethercat/apply_nm_unmanaged.sh` |
| 状态机 / PDO / WKC | 有 SubDevice 后用 SOEM `slaveinfo` / 示例验证 |
| 不自写主站栈 | 使用已克隆的 SOEM；**勿改**第三方 `SOEM/` 源码 |

## 10. FAQ 风格常见坑（读公开资料时）

1. **“工业以太网就是 EtherCAT？”**  
   否。Modbus TCP、普通 TCP 诊断口、PROFINET、EtherCAT 都可能跑在 RJ45 上，合同完全不同。

2. **“主站 sleep 很准 = 从站同步很好？”**  
   否。DC 是从站硬件时间域的事；主站唤醒 jitter 是另一份证据。

3. **“进了 OP = 安全？”**  
   否。OP 是通信/应用层状态；功能安全要独立安全回路与认证叙述。本仓不做后者。

4. **“WKC 偶尔少 1 可以忽略？”**  
   预习结论：周期数据面不应把坏 WKC 当好数据。恢复策略是工程选择，不是“ squelch 计数器”。

5. **“用 ESP32/普通 PHY 自己做一个从站练手？”**  
   对主站学习路径通常不划算：缺合格 ESC 就不是同一问题。本仓首轮买资料完整的 I/O SubDevice。

6. **“SOEM `No slaves found` 是总线坏了？”**  
   未接从站或 carrier off 时，这常是**预期**；只说明适配器打开路径，不是故障 PASS/FAIL。

7. **“Gate 里 raw bind pass 了，主站功能验完了？”**  
   否。见 Gate G1–G6：那是**网卡前置条件**，不是 PDO/OP/WKC。

8. **“SDO 和 PDO 哪个更‘实时’？”**  
   PDO 走周期映像；SDO/邮箱是配置与诊断。不要用 SDO 轮询冒充闭环。

## 11. 和 Modbus TCP 差在哪（初学者对照）

两者都常出现在“工业设备通信”面试，但**不是同一层问题**：

| | EtherCAT | Modbus TCP（本仓实验） |
|---|---|---|
| 直觉 | 一帧过程映像，从站硬件 on-the-fly | 主站问、从站答的寄存器读写 |
| 承载 | 以太网帧 `0x88A4`（通常要 raw / AF_PACKET） | 普通 TCP 字节流 + MBAP 定界 |
| “一笔成功” | 帧回主站且 **WKC** 符合期望 | TransID 匹配且 PDU 合法 / 非 exception |
| 半包问题 | 二层帧边界清晰；重点在映射与状态 | TCP 流必须按 Length 组帧 |
| 周期角色 | 闭环过程数据面（PDO） | 外围轮询 / 配置面；**不**代替 1 ms Runtime |
| 从站硬件 | 需要 ESC | 普通 TCP server / 设备栈即可 |
| 本仓状态 | 预习笔记 + NIC Gate；联调 SubDevice 另开 | localhost codec/framer **使用过** |

先读 `docs/MODBUS_TCP_NOTES.md` / `KNOWLEDGE_BASE.md` §6.11 建立“问答应答 + 半包组帧”
直觉，再回到本笔记的帧/WKC/状态机，不易把两种协议混成“都是工业以太网所以一样”。

## 12. 不能声称

- 读完公开网页 = 读完成员版 Compendium 全文或 IEC 原文；
- 理解 WKC = 已在实物上测过掉线恢复；
- 主站软件可进 OP = 硬实时或功能安全；
- NIC Gate G1–G5 通过 = SubDevice OP / PDO / 周期合格；
- ESP32/STM32 + 普通 Ethernet = 合格 EtherCAT SubDevice；
- ThinkPad `e1000e` 基线 = Orange Pi 板载网口同结论（平台证据必须分开写）。
