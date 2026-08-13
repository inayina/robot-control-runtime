# STM32F103C8T6 物理 CAN V1 节点与 SG90 双位置扩展 SPEC

状态：**CAN/PC13 physical smoke PASS；SG90 双位置目视 smoke PASS、波形未测；full acceptance PARTIAL**  
日期：2026-08-13  
线级 authority：[`protocol/can_v1/README.md`](../../protocol/can_v1/README.md)  
硬件角色：Orange Pi `can0` 的第二个 active CAN peer；不替代 Linux Runtime，不属于安全控制器

## 1. 要回答的工程问题

本实验把已经在 `vcan` 上验证的 CAN V1 节点行为迁移到一个独立 MCU，回答：

1. Orange Pi 4 Pro 的 MCP2515/SocketCAN `can0` 能否与 STM32F103 bxCAN 在真实线缆上双向通信；
2. heartbeat、session、sequence、OutputStatus 和普通输出 lease 是否跨 Linux/MCU 时钟边界成立；
3. CAN RX 中断、主循环协议处理和本地单调时钟能否形成职责清晰、可测量的最小节点；
4. 断线、bus-off、MCU 复位和命令租约到期时，逻辑输出能否归零且不自动重放旧命令；
5. STM32 能否用硬件定时器把一个已接受的离散输出位稳定映射为两档舵机 PWM，并在租约
   失效时停止控制脉冲。

已实现基线把 `output bit0` 映射到 Blue Pill 板载 PC13 LED。SG90 扩展保持同一个 CAN V1
离散位合同，并把该位映射到 PA8/TIM1_CH1 的两个保守 PWM 目标。它只驱动无负载 SG90，
不验证连续角度控制、位置闭环、机械负载性能、功能安全、硬件急停、RTOS 或硬实时。
SG90 不向本节点返回实际角度，`output_mirror` 只能证明 STM32 接受了目标状态，不能证明舵机到位。

## 2. 方案与备选

### 2.1 采用方案：自包含裸机 C11

- 独立 CMake 工程，使用 `arm-none-eabi-gcc`；不由 `linux/` CMake 递归构建；
- 仓内保存 startup、linker script、最小寄存器定义、CAN V1 codec 和节点状态逻辑；
- 主机编译同一份纯 C codec/node logic，先跑确定性单测，再交叉编译固件；
- 不使用动态内存、异常、RTOS、printf 或运行时文件系统。

这样可以固定依赖版本并直接观察 Cortex-M3 启动、NVIC、SysTick、bxCAN 和中断/主循环边界。

### 2.2 未采用：STM32CubeIDE/HAL

HAL 能更快生成时钟和外设初始化，但需要外部 STM32CubeF1 包及生成器版本；当前仓库无法只靠
checkout 复现。第一版只有一个 MCU、一个 CAN 外设和一个 LED，暂不值得引入该依赖。

若以后加入复杂板级外设或量产初始化，重新评审 HAL/LL；不能同时维护两套等价驱动。

### 2.3 SG90 扩展采用方案：TIM1 硬件 PWM

- PA8 使用默认复用功能 `TIM1_CH1`，TIM1 独占生成 50 Hz PWM；
- 主循环只在合法 CAN 命令或失效事件发生时更新预装载比较值，不用延时循环产生脉冲；
- `bit0=0` 映射为 1.25 ms 高电平，`bit0=1` 映射为 1.75 ms 高电平；
- lease 到期、复位、bus-off 或内部故障时比较值归零，PA8 保持低电平，不再输出舵机控制脉冲；
- PC13 保留为 bit0 的诊断镜像，不承担舵机供电或 PWM 输出。

未采用 SysTick ISR 或主循环软件翻转 GPIO。软件 PWM 会把 CAN 解码、队列泵送和中断延迟
直接带进脉宽，令故障时序和波形抖动更难界定；TIM1 已能在不增加 RTOS 或第二套调度器的
情况下独立完成周期输出。

## 3. 硬件冻结与接线

### 3.1 已确认

| 项 | 冻结值 |
|---|---|
| MCU | STM32F103C8T6 Blue Pill |
| 调试器 | ST-Link / SWD |
| CAN 收发器 | 3.3 V SN65HVD230 模块 |
| MCU CAN | bxCAN1，默认引脚 PA11/PA12 |
| 物理总线 | 经典 CAN，500 kbit/s，标准 11-bit ID |
| 终端 | 总线两个物理末端各 120 Ω；断电 CANH-CANL 期望约 60 Ω |
| 诊断输出 | PC13 板载 LED，active-low，继续镜像 output bit0 |
| 舵机信号 | PA8 / TIM1_CH1，3.3 V 逻辑，50 Hz |
| 舵机 | SG90 或明确兼容品；首次只做无舵盘、无机械负载测试 |
| 舵机供电 | 独立稳压 5 V；禁止从 Blue Pill 3.3 V 或 ST-Link 取电；控制侧共地 |

用户已报告 PA8、独立 5 V 和共地线路接好；这只是接线声明，尚未形成电压、极性、波形或
舵机型号证据。首次给舵机上电前仍必须执行 3.3 节检查。

### 3.2 接线表

```text
Orange Pi 4 Pro + MCP2515 HAT                    STM32F103 peer

      CANH o───────┬════════════ 双绞线 ════════════┬───────o CANH
                   │                                │
              [120 Ω，HAT 端]                  [120 Ω，MCU 端]
                   │                                │
      CANL o───────┴════════════════════════════════┴───────o CANL
       GND o──────────────────────────────────────────────────────────o GND

                                                        SN65HVD230        Blue Pill
                                                             TXD/D o──── PA12 CAN_TX
                                                             RXD/R o──── PA11 CAN_RX
                                                               VCC o──── 3V3
                                                               GND o──── GND

ST-Link: SWDIO ─ PA13，SWCLK ─ PA14，GND ─ GND，NRST 可选

Blue Pill PA8 / TIM1_CH1 ────────────────────────────────o SG90 signal
独立稳压 5 V 正极 ───────────────────────────────────────o SG90 V+
独立稳压 5 V 负极 ───────────────┬───────────────────────o SG90 GND
                                 └──────── Blue Pill / CAN GND
```

图中的电阻跨接在各端的 CANH 与 CANL 之间，不是串联在线路中。若 HAT 和 SN65HVD230 模块
已经各有一个固定 120 Ω，就不能再外接第三个。

| Blue Pill / Orange Pi HAT | SN65HVD230 / 总线 | 约束 |
|---|---|---|
| `PA12 / CAN_TX` | `TXD` / `CTX` / `D` | MCU 推送到收发器；不得接到 RXD |
| `PA11 / CAN_RX` | `RXD` / `CRX` / `R` | 收发器推送到 MCU；3.3 V 逻辑 |
| `3V3` | `VCC` | 禁止给 3.3 V SN65HVD230 模块输入 5 V |
| `GND` | `GND` | MCU 与收发器共地 |
| Orange Pi HAT `CANH` | MCU 端 `CANH` | 与 CANL 使用双绞线 |
| Orange Pi HAT `CANL` | MCU 端 `CANL` | 不得交换 CANH/CANL |
| Orange Pi HAT `GND` | MCU/收发器 `GND` | 当前非隔离短距离台架需要参考地 |
| `PA8 / TIM1_CH1` | SG90 signal（通常为橙/黄线） | 只传 3.3 V PWM 控制信号；颜色不能替代引脚核对 |
| 独立稳压 `5V` 正极 | SG90 V+（通常为红线） | 禁止接 Blue Pill 3.3 V、USB 或 ST-Link 电源 |
| 独立稳压 `5V` 负极 | SG90 GND（通常为棕/黑线） | 必须与 Blue Pill、SN65HVD230 和非隔离 CAN 参考地共地 |

SN65HVD230 的 `RS` 决定高速/斜率控制/待机行为。首轮上电前必须从模块原理图或实际电阻走线
确认它没有悬空，并记录是直接下拉还是通过电阻接地。商品芯片名不能替代该检查。

ST-Link 最小连接为 `SWDIO→PA13`、`SWCLK→PA14`、`GND→GND`，`NRST` 可选。目标供电只保留
一个明确来源；廉价 ST-Link 上标作 `3.3V` 的脚可能是电源输出而不是电压参考，未核对前不把
两个电源输出并联。PA11/PA12 同时是 USB D-/D+，本实验不启用 USB 外设。

### 3.3 断电检查停止线

烧录和总线上电前：

1. 断开所有电源，确认 VCC-GND 无明显短路；
2. 确认总线上只有两个 120 Ω，测得 CANH-CANL 约 60 Ω；
3. 核对 Orange Pi HAT 和 SN65HVD230 模块是否各自已经固定接入 120 Ω，避免第三个端接；
4. 核对 SN65HVD230 `RS` 状态和 PA11/PA12 方向；
5. 舵机卸下舵盘或脱离机构；先断开舵机电源，用万用表确认 5 V 极性和电压；
6. 优先在舵机未上电或信号线断开的状态测量 PA8 波形，再连接舵机；
7. 先只连接 ST-Link 验证固件，再连接 CANH/CANL/GND；出现器件发热、异常电压、抖动、
   顶死或反复复位立即断电。

建议在舵机电源接口附近放置 470–1000 µF 电解电容和 100 nF 陶瓷电容。具体电源电流余量
以实际舵机型号和测量为准；本 SPEC 不把未知 5 V 电源自动判定为足够。PA8 的 3.3 V PWM
是否被所购舵机可靠识别也必须实测，不得通过向 STM32 GPIO 输入 5 V 来解决兼容问题。

## 4. 时钟与 CAN bit timing

固件明确假设 Blue Pill 焊接 **8 MHz HSE 晶振**：

```text
HSE 8 MHz × PLL9 = SYSCLK 72 MHz
AHB                 = 72 MHz
APB1 / 2            = 36 MHz  ← bxCAN kernel clock
APB2                = 72 MHz
```

bxCAN 500 kbit/s 使用：

```text
BRP=4, SJW=1 tq, BS1=15 tq, BS2=2 tq
总时间量子 = SyncSeg 1 + BS1 15 + BS2 2 = 18 tq
bitrate = 36 MHz / 4 / 18 = 500 kbit/s
sample point = (1 + 15) / 18 = 88.9%
```

若 HSE 在有界等待内未 ready，固件不得用另一时钟偷偷以错误 bitrate 运行：保持输出关闭并停止
进入 CAN 正常态。若实物不是 8 MHz 晶振，必须重新计算配置并修改本 SPEC。

SysTick 使用 72 MHz 内核时钟产生 1 ms 单调 tick。该 tick 只在本次启动内有效，不与 Linux
共享绝对时间；V1 的 `validity_10ms` 在接收时转换为 MCU 本地 deadline。

TIM1 位于 APB2，计数时钟为 72 MHz。SG90 扩展冻结为：

```text
prescaler = 71       → 1 MHz 计数频率，每计数 1 us
ARR       = 19999    → 20000 us 周期，即 50 Hz
CCR1      = 1250     → 位置 A：1.25 ms 高电平
CCR1      = 1750     → 位置 B：1.75 ms 高电平
CCR1      = 0        → PWM disabled：PA8 持续低电平
```

TIM1 使用 PWM mode 1、ARR/CCR1 preload，并在 20 ms 周期边界提交目标，避免在当前脉冲中途
改变比较值。1.25/1.75 ms 只是保守的双位置目标，不标注成精确角度，也不自动扩展到舵机端点。

## 5. 软件分层与资源 ownership

```text
CAN1_RX0 IRQ
  → 读取 bxCAN FIFO0
  → 附 receive_ms
  → 固定容量 SPSC RX queue
                         main loop
                         ├─ CAN V1 decode
                         ├─ session/sequence/lease 状态
                         ├─ heartbeat/status/OutputStatus encode
                         └─ 固定容量 TX queue → bxCAN mailbox

SysTick IRQ → 只递增 uint32_t monotonic_ms
PC13       ← output_mirror bit0
PA8/TIM1   ← lease 有效时的双位置目标；lease 无效时 CCR1=0
```

| 组件 | owner | 不做什么 |
|---|---|---|
| CAN RX ISR | bxCAN FIFO0、RX queue producer | 不解码协议、不等待发送、不打印 |
| SysTick ISR | 单调毫秒计数 | 不运行节点状态机 |
| main loop | codec、session/sequence、lease、TX queue、PC13、TIM1 目标提交 | 不阻塞等待 CAN ACK，不软件生成 PWM |
| bxCAN TX mailbox | main loop 的非阻塞 pump | 不在 ISR 中无限重试 |
| TIM1_CH1 | 50 Hz 周期和 PA8 波形 | 不解释 CAN，不拥有 session/lease 决策 |
| Linux Runtime | SocketCAN、supervision、命令发布 | 不共享 MCU struct/绝对时间 |

RX 队列使用单生产者（ISR）/单消费者（main）索引。槽内容写完后才发布 head；main 消费后才
推进 tail。队列满不得覆盖旧帧：节点立即把逻辑输出归零、撤销 lease，并锁存 `INTERNAL`。

## 6. 会话、状态与线级行为

节点固定 `node_id=1`：

| 方向 | 消息 | CAN ID | 周期/触发 |
|---|---|---:|---|
| MCU → Linux | NodeHeartbeat | `0x021` | 100 ms |
| MCU → Linux | NodeStatus | `0x041` | 100 ms |
| Linux → MCU | OutputCommand | `0x061` | 命令触发 |
| MCU → Linux | OutputStatus | `0x081` | 每条成功解码的命令 |

所有帧都是经典 CAN 2.0A、标准 ID、DLC=8、大端字段。固件不得包含 Linux C++ 类型，也不得
对 C struct 做 `memcpy` 上总线。

### 6.1 boot/session

每次启动从最后一个 flash page 的小型追加日志获取下一个非零 u16 计数；`boot_id` 与
`session_id` 首版使用同一值（协议允许相等）。应用链接区域不占用该 page。

- 普通复位追加一个记录，不擦整页；
- journal 满时才擦除并从下一个非零值继续；
- flash 更新失败时使用非零占位 session，但 `interlock_ready=false`、`fault=INTERNAL`，拒绝输出；
- ST-Link mass erase 会清除 journal，属于重新烧录/commissioning 边界，验收时必须重启 Linux 侧会话；
- 该单页 journal 不宣称抗任意掉电的工业级持久计数，正式产品应使用双页事务或外部持久身份。

### 6.2 命令处理顺序

对发往 node 1 的合法 OutputCommand，固定按以下顺序判定：

1. 帧层、版本、DLC、mask、sequence、validity 合法性；否则静默拒绝并计数；
2. session 不匹配 → `SESSION_MISMATCH`；
3. 节点未 ready → `NOT_READY`；
4. sequence 不是相对上次已接受序号更新 → `STALE_SEQUENCE`；
5. `now_ms >= receive_ms + validity_10ms × 10` → `EXPIRED`；
6. 应用 mask/value、建立同一个 lease deadline → `APPLIED`。

只有 `APPLIED` 推进 last sequence、更新 output mirror 和刷新 lease。任何拒绝不得延长旧输出。
lease 到期时 output mirror 归零、PC13 熄灭，但不清 last sequence；恢复后必须收到当前 session
下更新序号的新命令。

### 6.3 SG90 双位置设备语义

SG90 不是 CAN 舵机；它只看到 PA8 上的 PWM。CAN V1 `OutputCommand` 不新增角度字段，设备侧
固定解释如下：

| `mask` bit0 | `values` bit0 | lease 有效时的行为 |
|---:|---:|---|
| 0 | 任意 | 不改变当前舵机目标；整条命令仍必须有其他非零 mask bit，否则按 CAN V1 拒绝 |
| 1 | 0 | 目标 A：20 ms 周期、1.25 ms 高电平；PC13 OFF |
| 1 | 1 | 目标 B：20 ms 周期、1.75 ms 高电平；PC13 ON |

SG90 验收命令源固定使用 `mask=0x01`，避免其他输出位令测试含义含混。CAN V1 当前只有一条
覆盖全部普通输出的 lease；因此任何合法 `APPLIED` 命令都会刷新该 lease，即使它没有更新
bit0。这是冻结合同的现有行为，不在设备侧偷偷增加第二套舵机超时。

只有 `APPLIED` 才能原子提交新的 CCR1 目标和同一条命令的 lease deadline。输出切换在下一个
TIM1 update event 生效，最迟不超过一个 20 ms PWM 周期。lease 失效时 `output_mirror=0`、
PC13 OFF、CCR1=0；因此 mirror 为 0 可能表示“有效的位置 A”，也可能表示“当前没有 PWM
lease”，不能单独用 mirror 或 NodeStatus 判断舵机是否仍被驱动，必须结合已接受命令和本地
lease 时间线。若未来必须从线级直接观察 PWM-active 状态，应先扩展冻结协议，而不是复用
`input_bits` 暗示位置反馈。

若未来需要连续角度、速度或位置反馈，必须设计带单位、范围和 golden vectors 的新消息，
不能把 V1 的一个输出位静默重新解释为角度值。

## 7. 失败行为

| 条件 | 固件行为 | 可声称边界 |
|---|---|---|
| 上电/复位 | PC13 OFF、CCR1=0；新 boot/session；无旧输出恢复 | 软件默认无控制脉冲，不是安全输出 |
| 非法帧 | 不改输出、无无法可信构造的 ACK、拒绝计数增加 | codec fail-closed |
| 错误 session/旧序号 | 返回明确 OutputStatus，输出和 lease 不变 | 协议拒绝可观察 |
| lease 到期 | output mirror=0、PC13 OFF、CCR1=0 | 最迟在下一个 PWM 周期停止控制脉冲 |
| RX/TX 软件队列满 | 输出归零、CCR1=0、取消 lease、锁存 INTERNAL | 不静默覆盖；需复位恢复 |
| bxCAN bus-off | 输出归零、CCR1=0、取消 lease、暂时 not-ready；ABOM 尝试控制器恢复 | 不等于布线已恢复 |
| bus-off 恢复 | ready 后仍保持 output=0、CCR1=0，等待新命令 | 不重放旧目标 |
| main loop 卡死 | IWDG 超时复位，复位入口先保持 PC13 OFF、CCR1=0 | 设计行为，须上板测量 |

CAN 控制器自动重发和 ABOM 只处理链路层；它们不能替代 Runtime heartbeat、节点 lease 或显式
故障恢复。无 peer 时发送缺少 ACK，可能不断累计错误甚至 bus-off，不能把 `cansend` 返回成功
写成对端已经收到。

停止 PWM 可能让舵机释放保持力，在有负载时不必然是机械安全状态。本实验只允许无舵盘、
无机械负载台架；软件关闭、PC13 LED 和 CAN 状态都不得描述成硬件急停或安全保证。

## 8. 构建、烧录与验证 Gate

### G0：主机纯逻辑

- host C 编译开启 warnings-as-errors；
- codec 与 CAN V1 golden bytes 一致；
- session、sequence、partial mask、lease 边界和拒绝不刷新 lease 有确定性单测。

### G1：ARM 产物

- `arm-none-eabi-gcc` 交叉构建通过；
- 生成 ELF/BIN/HEX/map；
- ELF 的 flash/RAM 不越过 STM32F103C8T6 的 64 KiB/20 KiB 边界；最后 1 KiB 保留给 session journal。

### G2：单板上电（CAN 线暂不接）

- ST-Link 能烧录、复位和停在 main；
- HSE/PLL ready；PC13 默认 OFF；
- SysTick 为 1 ms；IWDG 正常喂狗，主动停止喂狗会产生新 boot/session。

### G3：只读总线观察

- Orange Pi `can0` 与 MCU 都设为 500 kbit/s；
- 先由 Orange Pi `candump -t a can0` 观察 `0x021/0x041`；
- 保存 `ip -details -statistics link show can0` 前后错误计数；
- 未看到稳定 heartbeat 前不发送 OutputCommand。

### G4：命令/应答

- 使用当前 session、严格递增 sequence、`validity_10ms=30`；
- bit0=1 时 PC13 ON，收到匹配 `APPLIED`；刷新停止后约 300 ms 输出归零；
- 错误 session、重复 sequence、非法 mask 均不改变 PC13；
- 对齐 `candump`、OutputStatus 和 LED 现象，LED 照片不能替代帧证据。

### G5：故障

- 断开 CANH/CANL、停止命令流、复位 MCU、制造错误 session；
- 保存 MCU 重启后的新 session、Orange Pi heartbeat timeout、CAN error counters 和恢复后的新命令；
- 未显式恢复前不得自动点亮 LED。

### S0：PWM 电气基线（SG90 扩展实现后）

- 舵机未上电或信号线断开时发送合法 A/B 命令；
- 逻辑分析仪或示波器捕获 PA8 的 20 ms 周期、1.25/1.75 ms 高电平和 CCR1=0 关闭状态；
- 每个目标至少观察 10 s，保存原始波形及工具测量误差；
- 未拿到正确波形前禁止给舵机上电。

### S1：无负载双位置

- 拆下舵盘或保证没有机械负载，确认独立 5 V 极性、共地和供电无明显跌落；
- Linux 每 100 ms 用当前 session 和新 sequence 刷新命令，`validity_10ms=30`；
- A/B 每 2 s 切换一次，连续 10 个循环；对齐 OutputStatus `APPLIED`、PC13、PA8 波形和目视动作；
- 目视动作不等于位置反馈，不能记录为“达到指定角度”。

### S2：舵机输出失效路径

- 分别停止命令流、断开 CAN、复位 STM32，并制造错误 session/陈旧 sequence；
- lease 到期、bus-off、内部故障或复位后 PA8 必须停止控制脉冲，恢复时不得重放旧目标；
- 保存 CAN 帧、接口错误计数和 PWM 关闭时间线；只有舵机视频不能关闭本 Gate。

## 9. 当前证据边界

2026-08-13 已使用 ST-Link 烧录并 verify；HotPlug 读回向量/session，Orange Pi `can0` 已观察到
STM32 的 100 ms heartbeat/status，并完成 APPLIED、lease 后 mirror 归零、STALE_SEQUENCE 和
SESSION_MISMATCH 双向 smoke。证据见
[`evidence/stm32f103_can/README.md`](../../evidence/stm32f103_can/README.md)。

2026-08-13 的补充测试使用当前 session 连续刷新合法命令约 30 s，保存到序号
`0x0016..0x004E` 的连续 `APPLIED + mirror=1` 响应，用户同时目视确认 PC13 LED 点亮。该结果
可支持“CAN 命令到 PC13 输出链路通过”，但没有照片或电气波形，不能替代 PA8/SG90 验收。

全部证据来自 dirty implementation tree，只能标为本地物理 smoke。模块正反面、`RS` 电阻连接、
断电 60 Ω 和 HSE 频率没有形成保存证据；断线、复位、bus-off、IWDG、`rcrd --can can0` 和完整
physical fault matrix 仍未运行。SG90 接线最初由用户报告完成；TIM1/PWM、具体双位置映射及
主机测试已实现，3460-byte ARM BIN（sha256 `c7050c1af4e7ff7958dfab24989e4e16c22560a0bcc8a655582e9b4cfa4dd9c8`）
已经由 CubeProgrammer 2.22.0 download/verify/reset。复位后新 session=3，约 3 s heartbeat/status
稳定，`can0` 保持 ERROR-ACTIVE 且错误计数为 0。经用户明确允许后，session=3 下完成一次
受控 A→B 命令：sequence 1–30 全部 `APPLIED + mirror=0`，31–60 全部
`APPLIED + mirror=1`；停止刷新后重复 sequence 60 返回 `STALE_SEQUENCE + mirror=0`，接口
错误计数仍为 0。该结果证明 CAN/节点逻辑的 A/B 目标与 lease 收敛，不证明 PA8 实际脉宽；
随后以 sequence 61–120 执行 B→A→B 复测，用户目视确认出现两次方向相反的转动。该结果只把
S1 的“无负载双位置目视动作”标为 operator-observed smoke PASS；没有位置反馈，不能声称达到
精确角度。S0 波形未运行，S2 只有协议 lease 归零、没有 PA8 关闭时间线，仍为 partial。因此
不能把它写成完整 hardware acceptance、Runtime B4、硬实时或安全证明。

同日还单独烧录一次性仲裁诊断固件：STM32 以标准帧 `0x7FE`、`NART=1` 与 Orange Pi 的
`0x001` 竞争，30,000 次尝试中由 bxCAN mailbox 直接记录 `TXOK=29963`、`ALST0=37`、
`TERR=0`，Linux 侧错误帧为 0。该项只把“物理总线上存在无破坏仲裁失败者”标为 smoke PASS；
没有位级波形、严格同时起帧、最坏仲裁延迟或 bus-off 证据。测试后已恢复并 verify 正常
CAN/SG90 固件，完整计数和固件 hash 见上述 evidence 摘要。
