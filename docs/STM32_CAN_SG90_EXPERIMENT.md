# STM32F103 CAN + SG90 双位置实验设计

状态：**Proposed / Not started（仅设计，未采购、未接线、未编写固件、未形成实物证据）**  
所属阶段：阶段 7“可选外围实物通信”，不属于 V1 退出条件  
协议基线：[CAN Runtime V1 线级合同](../protocol/can_v1/README.md)（Frozen）

## 1. 要回答的工程问题

这个实验只验证一条小而完整的物理执行链：

```text
Linux 实验命令源
  → rcr Runtime / SocketCAN
  → 有明确 Linux SocketCAN 驱动的 USB-CAN
  → CANH / CANL / GND
  → SN65HVD230
  → STM32F103 CAN 节点
  → TIM1_CH1 50 Hz PWM
  → 无负载 SG90
```

它要回答：

- 冻结的 CAN V1 合同能否从 `vcan` 迁移到真实 CAN 电气链路；
- Linux 与 MCU 之间的 heartbeat、session、sequence、相对有效期和重启恢复是否成立；
- MCU 能否把一个已接受的离散输出位稳定转换成两档 PWM；
- Linux 失联和 CAN 断线时，Linux 监督与 MCU 本地输出监督能否分别收敛；
- CAN 端接、错误计数、bus-off、断线恢复和 PWM 波形能否留下可复测证据。

它不验证连续角度控制、舵机位置闭环、电机控制算法、机械负载性能、功能安全、硬件急停、
PREEMPT_RT 或硬实时。SG90 没有送回本实验的实际角度反馈，`output_mirror` 只能表示 MCU
接受并保存的命令状态，不能写成“舵机已到达目标角度”。

## 2. 分层职责与不新增的抽象

| 层次 | 本实验组件 | 职责与边界 |
|---|---|---|
| 物理/驱动 | USB-CAN、CAN 双绞线、SN65HVD230、Linux CAN 驱动 | 电平转换、收发比特、错误状态和 SocketCAN `can0`；不解释业务含义 |
| 线级协议 | CAN V1 codec | 校验 11-bit ID、DLC、版本、字节序、session、sequence 和 validity；不生成 PWM |
| 设备语义 | STM32 节点状态与双位置映射 | 把 `OutputCommand` bit 0 映射成 A/B 两个脉宽，维护本地命令租约并上报状态 |
| Runtime | `rcrd` / 实验验收进程 | 监督节点 heartbeat、会话、故障和状态迁移；不直接拥有 MCU 定时器 |
| 应用 | 专用实验命令源 | 每 100 ms 发送新序号命令，安排 A/B 切换和故障用例 |

这里不提前创建“通用 Bus / Signal”框架。当前仓库只有 SocketCAN 这一条已进入 Runtime 的
真实 fd 数据流，尚不满足“至少两个行为不同的真实实现”这一抽象门槛。以后 Modbus 或
EtherCAT 真正进入 Runtime，再基于已经出现的共同点抽取接口；本实验不能为了展示分层而
改动现有 V1 主线。

独立运行的 `rcrd` 当前不会自动发送演示输出。实物验收必须使用一个边界明确的实验命令源，
经现有 Application 服务 API 周期提交命令；不能在生产 daemon 中留下隐蔽的舵机演示逻辑。

## 3. 复用 CAN V1，不偷换角度语义

默认 `node_id = 1`，直接复用现有四类经典 CAN 2.0A 标准帧：

| 方向 | 消息 | CAN ID |
|---|---|---:|
| STM32 → Linux | NodeHeartbeat | `0x021` |
| STM32 → Linux | NodeStatus | `0x041` |
| Linux → STM32 | OutputCommand | `0x061` |
| STM32 → Linux | OutputStatus | `0x081` |

V1 的 `OutputCommand` 仍是普通离散输出位。实验只定义设备侧映射：

| `mask` bit 0 | `values` bit 0 | STM32 行为 |
|---:|---:|---|
| 0 | 任意 | 保持当前目标不变 |
| 1 | 0 | 位置 A：20 ms 周期、1.25 ms 高电平 |
| 1 | 1 | 位置 B：20 ms 周期、1.75 ms 高电平 |

首次实验拆下舵盘或不连接机械负载。1.25 ms / 1.75 ms 是保守的两档脉宽目标，不把它们
标成精确角度；不同 SG90 或兼容品的机械范围、死区和端点可能不同。若以后确实需要连续
角度，必须新增带单位、范围和 golden vectors 的 CAN V2 消息或独立消息类型，不能把
V1 的一个输出字节静默改解释为“度”。

## 4. 时间、线程与资源所有权

### 4.1 Linux 侧

- Runtime 周期线程继续按现有合同执行监督，不为 SG90 改调度器；
- SocketCAN fd 仍由 I/O 线程和 `epoll` 路径拥有；
- 实验命令源每 `100 ms` 产生一条新命令，使用当前节点 `session_id` 和严格递增的非零
  `sequence`；A/B 目标每 `2 s` 切换一次；
- `OutputCommand.validity_10ms` 只表示一条命令在 MCU 接收后还有多久可以应用；它不是
  一个会持续自动失效的输出租约。

### 4.2 STM32 侧

- CAN RX 中断只做硬件状态读取、接收时间戳和定长帧入有界队列；不在中断里打印、动态
  分配内存或执行长协议流程；
- 主循环从有界队列解码 CAN V1、检查 session/sequence/validity，再原子更新 PWM 目标；
- TIM1 独占生成 `50 Hz` PWM，PA8 使用 `TIM1_CH1`；业务主循环不能软件延时模拟脉宽；
- SysTick 或另一硬件定时器提供 MCU 本地单调毫秒计时；不向 Linux 传绝对时间，也不假设
  Linux 与 MCU 的时钟同源；
- heartbeat 与 NodeStatus 各每 `100 ms` 发送；CAN 外设、RX 队列、PWM 定时器分别只有
  一个明确 owner；
- 启动、复位或内部故障时先关闭 PWM，再初始化 CAN 和周期上报。

### 4.3 节点侧命令租约

CAN V1 的相对有效期只检查“这条命令是否来得及执行”。为防止一条旧输出在通信中断后
无限保持，STM32 固件另设本地命令租约：

1. 每次成功接受合法 `OutputCommand` 时，把 `last_accepted_command_ms` 更新为当前 MCU
   单调时间；
2. 若连续 `300 ms` 没有接受到更新序号的合法命令，立即关闭 PWM，并在 NodeStatus 中
   上报 `COMM_LOSS`；
3. 重复序号、错误 session、过期或协议非法的命令不能刷新租约；
4. 恢复通信后不自动恢复旧 PWM。必须先完成 Runtime 显式恢复，再接受当前 session 下的
   新序号命令。

这项租约是待实现的 **STM32 设备行为**，不是当前 `rcrd` 或 CAN V1 已实现的功能。它与
Linux 侧默认 `300 ms` heartbeat timeout 分别监督相反方向：节点租约监督“命令有没有
继续到来”，Runtime heartbeat timeout 监督“节点有没有继续上报”。

## 5. BOM 清单

以下是设计 BOM，不是采购批准。USB-CAN 和市售 SN65HVD230 模块必须在购买前核对具体
型号、原理图、Linux 驱动、端接和电平连接。

| 数量 | 器件 | 最小要求 / 用途 |
|---:|---|---|
| 1 | STM32F103C8T6 Blue Pill | 现有板；使用 PA11/PA12 CAN、PA8 TIM1_CH1 |
| 1 | ST-Link | STM32 下载与调试；不能给 SG90 供电 |
| 1 | SN65HVD230 CAN 收发器模块 | 3.3 V 逻辑；确认引脚、是否带端接和待机脚状态 |
| 1 | Linux USB-CAN | 必须有明确 SocketCAN 驱动和错误帧支持；具体型号另审 |
| 1 | SG90 或明确兼容舵机 | 只做无负载、低行程观察；记录真实品牌与批次 |
| 1 | 稳压 5 V 电源 | 建议至少 2 A，优先 3 A；只按所购舵机实测电流定最终余量 |
| 2 | 120 Ω 端接 | 位于物理总线两端；若模块自带则不得重复并联 |
| 1 | 470–1000 µF 电解电容 | 靠近舵机电源接口，电压额定值留余量 |
| 1 | 100 nF 陶瓷电容 | 靠近舵机接口做高频去耦 |
| 1 套 | CAN 双绞线、地线、端子与跳线 | CANH/CANL 使用双绞线；非隔离台架同时连接参考地 |
| 1 套 | 保险丝、总电源开关或可快速断电接头 | 限制台架供电故障；不是认证急停 |
| 1 | 逻辑分析仪或示波器 | 测 PWM 周期/脉宽；示波器还可观察电源跌落与 CAN 波形 |

主要器件资料入口：

- [STM32F103x8/xB datasheet](https://www.st.com/resource/en/datasheet/stm32f103cb.pdf)
- [SN65HVD230 datasheet](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)
- [TowerPro SG90 产品页](https://towerpro.com.tw/product/sg90-7/)
- [Linux SocketCAN 文档](https://www.kernel.org/doc/html/latest/networking/can.html)

## 6. 接线图与上电检查

```text
Linux host
  USB
   │
USB-CAN                    STM32F103 Blue Pill
  CANH ───────────────────── CANH  SN65HVD230
  CANL ───────────────────── CANL  transceiver
  GND  ───────────────────── GND ───────┐
                                         │
                              PA12/CAN_TX ──> D / TXD
                              PA11/CAN_RX <── R / RXD
                              3V3 ───────────> VCC
                              PA8/TIM1_CH1 ───────────> SG90 signal
                                         │
independent regulated 5 V ───────────────┼──> SG90 V+
power supply GND ────────────────────────┴──> SG90 GND
                        470–1000 µF || 100 nF near SG90

  120 Ω at the USB-CAN end               120 Ω at the MCU end
```

逐线连接表：

| 起点 | 终点 | 说明 |
|---|---|---|
| STM32 PA12 / CAN_TX | SN65HVD230 D / TXD | MCU 到收发器 |
| STM32 PA11 / CAN_RX | SN65HVD230 R / RXD | 收发器到 MCU |
| STM32 3.3 V | SN65HVD230 VCC | 只给 3.3 V 收发器逻辑供电 |
| STM32 GND | SN65HVD230 GND | 共参考地 |
| USB-CAN CANH | SN65HVD230 CANH | 双绞线一芯 |
| USB-CAN CANL | SN65HVD230 CANL | 双绞线另一芯 |
| USB-CAN GND | STM32 / 收发器 / 舵机电源 GND | 非隔离短距离台架的参考地 |
| STM32 PA8 / TIM1_CH1 | SG90 signal | 只传控制信号 |
| 独立稳压 5 V 正极 | SG90 V+ | 禁止从 Blue Pill 3.3 V、USB 或 ST-Link 取舵机电源 |
| 独立稳压 5 V 负极 | SG90 GND | 与控制侧共地 |

首次上电前：

1. 全部断电，核对所购模块的真实引脚和原理图，不凭丝印颜色猜线；
2. 确认总线只有两个 120 Ω 端接，断电测 CANH-CANL 应接近 60 Ω；
3. 舵机拆下舵盘或脱离机构，PWM 默认关闭；
4. 先分别验证 3.3 V 和 5 V 电源，再连接 SG90；
5. 先跑 PWM-only，再跑 CAN-only heartbeat，最后合并，便于隔离电源、定时器和协议问题。

PA8 的 3.3 V PWM 是否被所购 SG90 可靠识别必须实测；不稳定时先停止实验并评审明确电平
规格的缓冲器，不能通过提高 STM32 GPIO 电压解决。

## 7. 节点状态与失败行为

| 条件 | STM32 PWM | STM32 上报 | Linux Runtime |
|---|---|---|---|
| 上电/复位 | 关闭 | 新 boot/session，周期 heartbeat/status | 识别新会话，不能自动恢复旧命令 |
| 在线但未激活 | 关闭 | `fault_code = NONE`，节点可监督 | 不进入或不保持 Active 输出 |
| 合法 A/B 命令 | 1.25/1.75 ms 目标 | OutputStatus `APPLIED` | 记录接受序号与镜像 |
| 重复或倒退序号 | 保持不变 | `STALE_SEQUENCE` | 不把拒绝当成功应用 |
| session 不匹配 | 保持不变 | `SESSION_MISMATCH` | 重新绑定当前会话 |
| 命令应用前已过期 | 保持不变 | `EXPIRED` | 记录超时，不重放旧命令 |
| 300 ms 无新合法命令 | 关闭 | NodeStatus `COMM_LOSS` | 由状态监督处理，不自动恢复 |
| CAN 线拔除 | 本地租约到期后关闭 | 无法送达，重连后报告 | heartbeat 超时后进入 CommLoss/Hold 路径 |
| MCU 内部故障 | 关闭 | 能发送时报告对应 fault | 离开 Active，需显式恢复 |

“关闭 PWM”在有负载时可能让舵机释放保持力，并不总是机械上的安全动作。因此本实验只在
无负载台架执行；上述软件行为不得称为安全状态、急停保证或功能安全机制。

## 8. 递进实验步骤

### P0：PWM-only 基线

1. 不连接 CAN、不安装舵盘/负载；
2. 固件上电保持 PWM 关闭；
3. 手动进入测试态，分别输出 20 ms / 1.25 ms 和 20 ms / 1.75 ms；
4. 用逻辑分析仪记录至少 10 s 周期、脉宽、启动和关闭边沿；
5. 若电源跌落、MCU 复位或波形异常，停止在 P0。

### P1：CAN 正常路径

1. 以选定比特率启动 `can0`，保存接口、驱动、bitrate 和 error counters；
2. STM32 每 100 ms 发送 heartbeat 和 NodeStatus；
3. 实验命令源每 100 ms 刷新合法命令，每 2 s 在 A/B 间切换；
4. 连续完成 10 个 A/B 循环，同时保存 `candump`、Runtime trace 和 PWM 波形；
5. 对齐 `sequence`、OutputStatus 和 PWM 变化，不能只拍“舵机动了”的视频。

### P2：协议拒绝

依次发送重复序号、倒退序号、错误 session 和应用前已过期的命令。每种情况下 PWM 目标
保持不变，OutputStatus 返回对应原因，非法命令不得刷新本地租约。

### P3：只停止命令流

保持 STM32 heartbeat/status 正常，只停止 Linux 命令刷新。设计门限为最后一条已接受命令
后 300 ms；PWM 必须在“300 ms + 一个主循环最大周期”内关闭并上报 `COMM_LOSS`。实际
延迟分布和主循环最大周期都要记录，不能把设计值冒充测量值。

### P4：拔除 CAN

拔开 CANH/CANL 或关闭接口，验证两个独立结果：STM32 本地租约到期后关闭 PWM；Linux
距最后合法 heartbeat 达到默认 300 ms 后进入 CommLoss/Hold 路径。恢复总线后仍保持
PWM 关闭，直到显式恢复并收到当前 session 的新命令。

### P5：STM32 复位

在位置 B 时复位 STM32。PWM 必须先关闭；新 boot/session 被 Linux 识别后 Runtime 离开
Active，旧 session 命令被拒绝。只有显式恢复和新序号命令才能重新输出。

## 9. 验收矩阵

| 用例 | 可复测 PASS 条件 |
|---|---|
| P0-A / P0-B | 捕获 20 ms 周期及 1.25/1.75 ms 目标脉宽；保存原始波形与测量误差 |
| 正常 A/B | 10 个循环中命令序号、`APPLIED` 和 PWM 目标一一对应，无未解释复位 |
| 陈旧序号 | 返回 `STALE_SEQUENCE`，PWM 目标不变，本地租约不刷新 |
| 错误 session | 返回 `SESSION_MISMATCH`，PWM 目标不变 |
| 过期命令 | 返回 `EXPIRED`，PWM 目标不变，本地租约不刷新 |
| 命令流停止 | 最后一条合法命令后在设计边界内关闭 PWM，NodeStatus 报 `COMM_LOSS` |
| CAN 断线 | MCU 本地关闭；Linux heartbeat timeout 可见；两边均不自动恢复输出 |
| STM32 复位 | PWM 先关闭，新 boot/session 可见，旧命令不重放 |
| 重连 | 未显式恢复前 PWM 保持关闭；恢复后只接受当前会话的新序号 |

## 10. 证据包

每次正式运行至少保存：

- Git commit、STM32 固件 commit/构建器版本及固件哈希；
- 主机、内核、CAN 接口型号、驱动、bitrate、端接和供电配置；
- 实物接线照片和实际 BOM 型号；
- 带时间戳的 `candump`、SocketCAN error frame / controller statistics；
- `rcrd` trace、状态迁移与退出结果；
- PWM 原始捕获、脉宽/周期统计和 P3/P4/P5 的故障时间线；
- 验收矩阵逐项 PASS/FAIL、异常解释和复跑命令。

视频只能辅助展示现象，不能替代帧、状态、波形和配置证据。模拟器、台架实物和以后目标
平台的结果必须分目录并显式标注，不能混成一组结论。

## 11. 开始门与停止条件

只有同时满足以下条件才进入采购或固件实现：

1. V1 与 Orange Pi 当前阶段退出条件已经完成，物理 CAN 被明确选为阶段 7 唯一优先链路；
2. USB-CAN 的 Linux SocketCAN 驱动、错误帧能力和具体型号已核对；
3. SN65HVD230 模块原理图、端接、待机脚和接口电平已核对；
4. 用户批准实际 BOM、供电和无负载台架；
5. STM32 固件采用独立原生构建/烧录流程，不被 `linux/` CMake 递归构建。

完成 P0–P5、验收矩阵和证据包后即停止。本阶段不顺手增加 AHT20/BMP280、RS-485、
Modbus RTU、EtherCAT、Dashboard、连续角度协议或机械负载。若物理 CAN 不再是求职证据的
最高优先缺口，则保留本文为设计，不因已经拥有 STM32 板卡而强行实施。

