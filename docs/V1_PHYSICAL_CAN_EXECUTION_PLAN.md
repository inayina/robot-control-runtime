# V1 收口 → BSP/Physical CAN → 真总线 Runtime 执行方案

状态：**Proposed / planning only**  
阶段定义冻结日期：2026-08-10  
硬件状态：面向 Raspberry Pi 40-pin 的 RS-485/CAN HAT 在途；准确品牌、SKU、版本、芯片、
原理图和 pinout **尚未冻结**

本文是后续开发的阶段编号权威。历史文档中的 P1/P2/P3 曾分别表示 daemon、ThinkPad
证据和 Orange Pi 部署工作包；这些旧编号只保留作历史证据索引，不再用于安排后续开发。

```text
P1 当前 V1 收口
ThinkPad vcan → fault handling → Release benchmark
                  → Orange Pi ARM / systemd evidence

P2 BSP / Physical CAN
到货识别 → CAN-enabled kernel → SPI/pinctrl → DTO
         → CAN controller probe → can0 → can-utils / peer

P3 Runtime 真总线验证
rcrd --can can0 → 第二 CAN 节点 → heartbeat/ACK/restart/recovery
                 → fault evidence
```

顺序是硬 Gate：P1 没有形成 clean、同 commit 的软件/平台基线，不开始内核或设备树开发；
P2 没有证明 `can0` 的驱动、电气和 peer 通信，不开始 Runtime 真总线结论。

## 1. 边界和已知事实

### 1.1 已知

- ThinkPad 已有 `vcan0`，用于完整 CAN V1、daemon、故障矩阵和进程生命周期验证。
- 现有 Orange Pi 4 Pro 4GB 证据记录的厂商内核为 `6.6.98-sun60iw2`，已有 ARM 构建、
  release/unit 安装和 12 格 pilot；该次内核记录为 `# CONFIG_CAN is not set`，P1 重采时
  仍须重新核对当前运行内核。
- 当前 Orange Pi 不能创建 `vcan0`，也没有 `can0`；`rcrd` 未作为 active service 常驻。
- 当前 CAN V1 是 classic CAN 2.0A、8-byte、显式大端 codec；物理阶段不重新设计协议。
- Runtime 的软件 EStop、Hold、Fault 和 ordinary-output lease 不等于硬件急停、STO 或功能安全。

### 1.2 到货前仍未知

“RS-485/CAN 转接板”只是商品类别，不足以决定 BSP。到货识别前以下字段均为 `TBD`：

| 字段 | 必须取得的证据 | 为什么会改变方案 |
|---|---|---|
| 准确品牌、SKU、硬件版本 | 包装标签、板卡正反面照片、商品链接 | 同名板可能换芯片或 pinout |
| CAN 控制器 | 芯片丝印、原理图 | 只有确认是 MCP2515 才走 `mcp251x`/SPI 路径 |
| CAN 收发器 | 芯片丝印、供电和逻辑电平 | 决定 3.3 V 兼容、待机脚和总线侧能力 |
| 晶振 | 丝印/原理图中的 8/16/20/40 MHz 等 | 错误频率会让 bitrate 全部错误 |
| SPI/CS/INT pinout | 原理图 + Orange Pi 40-pin 对照 | 决定 pinctrl、chip-select 和 IRQ |
| 电源与电平转换 | 原理图、万用表只读检查 | 禁止把 5 V SPI/INT 直接接 SoC 3.3 V GPIO |
| 端接 | 120 Ω 跳帽/电阻位置 | 总线只能在两个物理末端端接 |
| 隔离 | 隔离器和隔离电源器件 | 决定是否需要共参考地以及测试边界 |
| RS-485 部分 | UART、DE/RE、自动方向、终端/偏置 | 它是独立物理链路，不是 CAN peer |

如果实物不是 MCP2515 SPI CAN，而是 USB-CAN、SLCAN、UART 网关或其它控制器，P2 在
识别 Gate 停止并重写驱动路线；不得为了符合预案强行把它描述成 SPI3/MCP2515。

### 1.3 Raspberry Pi HAT 对 Orange Pi 的条件兼容

“标准 40-pin 外形”只说明机械位置相似，不保证复用功能、GPIO 编号、overlay 或驱动配置
兼容。到货前可形成的条件判断是：

| 条件分支 | 官方资料中的典型结构 | 对 Orange Pi 的影响 |
|---|---|---|
| Waveshare `RS485 CAN HAT` SKU 14882（若实物相符） | MCP2515 + SIT/SN65HVD230；CAN 走 SPI/CE0/INT；RS-485 为 SP3485/UART；现版通常 12 MHz，旧版曾有 8 MHz | 物理 SPI 常落在 19/21/23/24，INT 在 22；必须把物理 pin 重新映射成 A733 GPIO/IRQ，不能复制 BCM25 |
| Waveshare `RS485 CAN HAT (B)`（若实物相符） | CAN 仍为 MCP2515；RS-485 还包含 SC16IS752，两个功能均会占 SPI 资源；带隔离/宽压供电版本 | 需要同时审计多个 CS/IRQ、电源和隔离；不能沿用普通版的 UART/RSE 假设 |
| 其它品牌/版本 | `TBD` | 只按该板原理图和丝印重新规划 |

官方普通版资料：<https://www.waveshare.com/wiki/RS485_CAN_HAT>；(B) 版资料：
<https://www.waveshare.com/wiki/RS485_CAN_HAT_%28B%29>。这些链接只用于识别候选，不表示
用户购买的一定是 Waveshare。

Orange Pi 4 Pro 官方资料表明 40-pin 提供 SPI，手册中的 bring-up 路径使用 SPI3；但树莓派
`dtoverlay=mcp2515-can0,...interrupt=25` 不能移植，因为 `25` 是 Raspberry Pi BCM GPIO
编号。对 HAT 必须同时满足：物理 SPI pin 对齐、HAT 的 INT 物理 pin 能映射到可用 A733 GPIO
中断、CS 无冲突、所有输入电平兼容。任一项不满足时应使用 HAT 的 breakout 控制排针重新
接线，而不是继续直插。

Orange Pi 4 Pro 官方入口：<https://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/service-and-support/Orange-Pi-4-Pro.html>。

## 2. P1：收掉当前 V1

### P1-G0：工作树和阶段语义收敛

目标：先让将要测量的代码成为可归因的版本。

1. 审核当前 ACK、multi-fault recovery、output lease 和 fault-observability 变更的完整 diff；
2. README、SPEC、路线图统一采用本文的新阶段定义；历史计划加“旧编号”提示；
3. 修复 benchmark runner 把 `build_type=Debug` 写死的问题：必须从 CMake cache 或显式参数
   读取并校验，Release Gate 发现 Debug 时直接失败；
4. `git diff --check`、构建和测试通过后形成 clean commit；提交必须由用户明确授权；
5. 正式证据只接受 `git_dirty=false`，dirty 运行只作开发回归。

**Gate**：源码、二进制和证据都能指向同一个 commit；没有把旧 19 场景、旧 6 场景或
dirty pilot 写成当前 22/7 场景的正式证据。

### P1-G1：ThinkPad vcan / fault handling

同一 clean commit、主机实际 `vcan0` 下执行：

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
./build/linux/tests/test_socketcan_vcan --require-vcan
./linux/scripts/run_vcan_acceptance.sh vcan0
./linux/scripts/run_fault_matrix.sh vcan0
./linux/scripts/run_asan_ubsan.sh
./linux/scripts/run_tsan.sh
```

验收重点：

- 普通 CTest 不允许用内部 Skip 冒充强制 vcan Gate；
- 双进程验收覆盖正常命令、lease、中断 heartbeat、重启和非法帧；
- fault matrix 覆盖 multi-fault clear gate、ACK timeout=`Hold/AckTimeout`、worker failure、
  CommLoss、restart、SIGTERM；
- interface-down 用例会改变主机接口，只在显式授权的单独窗口运行，不放进默认 CTest；
- ASan+UBSan 必须有非空报告；LSan 若关闭必须记录；TSan 启动失败记 `unsupported`，不写 PASS。

**Gate**：当前预计 18 个 CTest target、7 个 vcan acceptance 场景和 22 个 fault-matrix
场景全部有实际运行结果；最终数量以当次程序输出为准，不在文档中反向凑数。

### P1-G2：ThinkPad Release benchmark

1. 先跑 5 秒 smoke，只验证 runner、权限和 `stress-ng`；
2. 冻结正式时长、CPU、governor、FIFO priority 和负载命令；
3. 执行 OTHER/FIFO × idle/stress × 1/5/10 ms 共 12 格；
4. 每格保存原始 lateness、P50/P95/P99/P99.9、miss、requested/enabled/error；
5. 记录内核、CPU、governor、温度、compiler、Release、commit/dirty；
6. 正式矩阵期间不同时运行编译、图形压测或其它未记录负载。

**Gate**：12 格各自只能是 `pass/failed/permission_denied/unsupported`；没有缺失格。结果只
证明普通 Linux 上空 callback 的唤醒 lateness，不是 CAN 往返、控制周期或硬实时证明。

### P1-G3：Orange Pi ARM / systemd clean evidence

Orange Pi 使用与 ThinkPad 相同的 clean commit：

1. 补齐板卡丝印、电源铭牌、NTP、降频/欠压和准确启动介质观察；
2. aarch64 原生 Release configure/build；运行非 CAN 测试，vcan/SocketCAN 项明确
   `unsupported/not_run`；
3. 安装 release、MANIFEST 和 `current`，校验运行二进制 SHA-256；
4. 复核 `rcr` 普通用户、unit 内容、日志入口、停止门限和 restart limit；
5. 当前无 CAN 内核下，保存 `rcr-vcan` 失败与 `rcrd` dependency inactive 的预期证据；
   不为全绿修改 daemon 或制造 FakeCan service；
6. 用修正后的 Release runner 重采与 ThinkPad 同条件的 12 格 ARM 矩阵；大小核选择必须
   由实际 CPU topology 决定，不能默认 CPU0；
7. 验证 release `current` 切换和回滚，不删除旧 release。

**P1 退出条件**：

- ThinkPad clean vcan/fault/sanitizer/benchmark 证据闭合；
- Orange Pi clean ARM build/release/systemd/benchmark 证据闭合；
- `CONFIG_CAN` 缺失、`vcan/rcrd active` 未执行被诚实保留为 P2/P3 的前置缺口；
- P1 发布摘要可以独立解释“软件功能在哪里测、ARM/部署在哪里测、什么还没有测”。

## 3. P2：BSP / Physical CAN

P2 只解决“Orange Pi 如何获得一个可信的 SocketCAN `can0`”。它不实现 MCU 业务协议，
也不把 RS-485 接进 Runtime。

### P2-G0：到货识别与无电检查

1. 保存包装、SKU、PCB 正反面和芯片近照；建立硬件 manifest；
2. 获取 vendor 页面、原理图、pinout、overlay/kernel 说明及文件 hash；
3. 以**物理 pin 号**对照 Orange Pi 4 Pro 40-pin，标出 3V3/5V/GND/SCLK/MOSI/MISO/CS/INT；
   Raspberry Pi BCM/WiringPi 编号只能作为 HAT 来源字段，不能写进 Orange Pi DTO；
4. 万用表断电检查端接电阻、明显短路和电源连接；不做带电连续性猜测；
5. 确认 CAN 与 RS-485 是两个独立控制器/收发器路径，分别记录，不共用协议结论。

**停止规则**：没有原理图或无法证明 SPI/INT 对 3.3 V 兼容时不插板上电；晶振、INT、CS
任一未知时不写 DTO。

执行时复制并填写
[`deploy/orangepi/PHYSICAL_CAN_BRINGUP_CHECKLIST.md`](../deploy/orangepi/PHYSICAL_CAN_BRINGUP_CHECKLIST.md)，
模板中的 `NOT_RUN` 不能作为通过项。

### P2-G1：可回滚的 CAN-enabled kernel

操作细则（今晚不安装）：见
[ORANGE_PI_CONFIG_CAN_PLAN.md](ORANGE_PI_CONFIG_CAN_PLAN.md)。首轮只开
`CONFIG_CAN`/`RAW`/`DEV`/`VCAN` 做板上 `vcan`；MCP2515 等到货识别后再开。

1. 保存当前 boot 配置、kernel、DTB、modules 和启动项快照；
2. 优先选择与板卡/SoC BSP 匹配、可回滚的 CAN-enabled kernel；若必须自编译，保持同一
   BSP 基线，首轮不同时引入 PREEMPT_RT；
3. 至少核对 `CONFIG_CAN`、`CONFIG_CAN_RAW`、`CONFIG_CAN_DEV`、SPI/GPIO/IRQ 和与实物控制器
   对应的驱动配置；MCP2515 才要求 `CONFIG_CAN_MCP251X`；
4. 安装到独立 boot entry，保留原厂内核作为已验证回退项；
5. 新内核先验证 SSH、存储、Wi-Fi/以太网、systemd、时钟和现有 Release benchmark smoke。

**Gate**：重启失败可返回旧内核；基础设备无未解释回归；CAN core 与目标驱动的配置来自
实际运行内核，而不是另一份 config 文件。

### P2-G2：SPI/pinctrl bring-up

1. 从运行 DT、Orange Pi 4 Pro 手册和 HAT 原理图确认目标 SPI controller；手册候选为
   SPI3，但仍必须由当前镜像的运行 DT/设备节点确认；
2. 确认 pinctrl group、CS、INT GPIO 编号、IRQ 极性和占用冲突；
3. 检查 `/sys/kernel/debug/pinctrl`、运行 DT、SPI device 和内核日志；
4. 若用 `spidev` 做短期电气诊断，完成后解绑；同一 CS 不能同时绑定 spidev 和 CAN 驱动；
5. 不用示波器/逻辑分析仪的“看见 SCLK”替代驱动 probe，但可用它定位无时钟/错误模式。

**Gate**：controller、pins、CS、INT 都有唯一映射；无 GPIO/CS 冲突和 5 V 风险。

### P2-G3：Device Tree Overlay

DTO 只表达实物事实：

- 控制器真实 `compatible`；
- 实际 chip select；
- 实测/资料确认的 oscillator clock；
- `spi-max-frequency`；
- interrupt parent、A733 GPIO 和触发类型；禁止复制 Raspberry Pi 的 BCM25 数字；
- 必需的 regulator/supply；
- 所用 SPI controller/pinctrl 的 enabled 状态。

具体属性以当前内核源码中的 binding 和基础 DTS 为准，到货前不预写可直接加载的 overlay。
保存 `.dts`、编译命令、`.dtbo` hash、加载配置和反编译结果；每次只改一个变量。

**Gate**：DTO 可移除并恢复原启动；运行 DT 与源码一致；没有用启动成功替代设备 probe。

### P2-G4：控制器 probe 与 `can0`

若确认是 MCP2515，期望 `mcp251x` 完成 SPI 通信、IRQ 和 netdev 注册。依次保存：

```text
dmesg 目标驱动片段
/sys/bus/spi/devices/...
/sys/class/net/can0
ethtool -i can0（若驱动支持）
ip -details -statistics link show can0
```

bitrate 只能在晶振和 peer 配置冻结后设置。首轮先保存 DOWN 状态和驱动信息，再执行
`ip link set can0 up type can bitrate ...`；失败时记录错误原文、IRQ 计数和 SPI 日志，不用
反复重启掩盖根因。

**Gate**：`can0` 来自预期 SPI device/driver，接口可重复 up/down，error counters 可读。

### P2-G5：can-utils 与真实 peer

分三层验收，不能互相替代：

1. controller internal loopback：验证 netdev/driver 软件路径，不证明收发器和布线；
2. listen-only/静态观察：验证接口能观察已知 peer 流量，不主动扰动；
3. 两节点真实收发：两端 bitrate 一致、总线两端各一个 120 Ω，保存 `candump/cansend` 和
   error counters before/after。

单控制器接上线但没有第二个 active CAN node 时可能没有 ACK；这不是 `cansend` 成功证据。
第二节点必须在 P2-G5 前冻结，优先级如下：

1. 已有、Linux 驱动明确的第二 SocketCAN 接口，用于先隔离 BSP/布线问题；
2. STM32F103 + 已核实的 3.3 V CAN 收发器，用于后续 CAN V1 节点；
3. ESP32-S3 + 3.3 V CAN 收发器作为备选，不同时维护两套 MCU 固件。

**P2 退出条件**：clean kernel/DTO/hardware manifest 可复现；`can0` probe、up/down、内部
loopback 和两节点物理收发均通过；没有 Runtime 或 MCU 业务成功的提前声明。

## 4. P3：Runtime 真总线验证

### P3-G0：先固定第二节点，不先接执行器

首选基线是一个只实现 CAN V1 的低风险节点：heartbeat、NodeStatus、OutputCommand、
OutputStatus、boot/session 和 ordinary-output lease。输出 bit 先映射 LED/逻辑状态，不接
舵机、电机或功率负载。

若采用 STM32F103：CAN RX 中断只做定长入队；主循环解码和更新状态；硬件计时产生本地
monotonic tick；不共享 Linux C++ struct，不加入 Linux CMake 超级构建。固件 fault injection
必须默认关闭，并与正常命令入口分离。

### P3-G1：`rcrd --can can0` 正常生命周期

1. 保留 P1 的 vcan unit 不动，新增物理部署专用的 root oneshot `rcr-can0.service` 和普通用户
   `rcrd-can0.service`；两套 unit 显式 `Conflicts=`，不能同时启用；
2. `rcrd --can can0` 以普通 `rcr` 用户启动，保存 socket 权限、started/final summary 和 fd；
3. 节点上线后观察 online、boot/session、interlock 和 heartbeat age；
4. stop/SIGTERM 有界，重启后不重放旧 session 命令；
5. systemd restart limit、release manifest 和当前 binary hash 与 P1 合同一致。

`rcr-can0.service` 只负责 bitrate、可选且已冻结的 bus-off recovery 参数和 link up/down，
因此需要 root/CAP_NET_ADMIN；`rcrd-can0.service` 不持有 CAP_NET_ADMIN。这里选择两个显式
物理 unit，而不是在 P1 unit 上叠加难以审计的 dependency drop-in；也暂不抽象通用 bus
service，因为当前只有一个物理接口实现。

独立 `rcrd` 不自动发送演示输出。heartbeat/restart 可由 daemon service 验证；需要命令和
ACK 的场景由专用 physical-CAN acceptance 程序通过现有 Application API 驱动，不在生产
daemon 增加隐藏测试开关。

### P3-G2：命令与 ACK 正常路径

1. acceptance 读取当前 node session，显式 Boot/Activate；
2. 发送带 sequence/deadline/mask/validity 的普通输出；
3. 只有 SocketCAN send 成功后建立 pending ACK；
4. 只有匹配 session/sequence 且结果为 `APPLIED` 才释放下一笔；
5. 对齐 Linux trace、`candump -t`、节点日志和 LED/逻辑状态；不把 `APPLIED` 写成物理动作完成。

### P3-G3：真总线故障矩阵

| 场景 | 注入方式 | Runtime 期望 | 节点/输出期望 |
|---|---|---|---|
| heartbeat loss | 节点停止 heartbeat 或断开总线 | CommLoss → Fault，关闭本地输出路径 | ordinary-output lease 到期归零 |
| ACK timeout | 节点保持 heartbeat，但测试态丢弃/延迟 OutputStatus | Hold + AckTimeout，不自动重试 | 不因异常 ACK 刷新权限 |
| node restart | 复位节点，boot/session 改变 | NodeFault/restart latch，旧 session 拒绝 | 启动时输出 neutral |
| stale/replay | 重发旧 sequence/session | 拒绝且不 kick watchdog | 返回相应拒绝，不刷新 lease |
| bus disconnect | 断开 CANH/CANL 或停止接口 | 必须出现 CommLoss；I/O 错误只按实际驱动事实记录，不预设 EPOLLHUP | 恢复后不自动恢复旧输出 |
| recovery | 根因恢复后显式 clear/resume/activate | 只先回 Idle，再新 Activate | 只接受当前 session 新命令 |

每次只注入一个主要变量；组合故障另列场景。拔线、接口 down、节点复位需要人工授权和
安全台架窗口，不进默认自动测试。

### P3-G4：fault evidence

每次报告至少包含：

- git commit/dirty、Linux kernel/config hash、DTB/DTO hash、driver、SPI device；
- HAT/转接板 SKU/版本、控制器/收发器/晶振、peer、bitrate、端接和接线图版本；
- `ip -details -statistics link show can0` before/after、相关 `dmesg`；
- Runtime mode/fault、worker/I/O、node latch、ACK/queue/trace counters 和 final summary；
- 注入动作的单调或可对齐时间、恢复动作、结果枚举和未执行项；
- 明确声明：普通 Linux、物理 CAN 与软件 lease 仍不是硬实时或功能安全证据。

**P3 退出条件**：同一 clean release 上，正常链路、heartbeat loss、ACK timeout、node
restart 和显式 recovery 均能重复；Runtime 与节点输出分别在其合同边界内收敛；systemd
冷启动能恢复 `can0` 与 `rcrd`，旧命令不重放。

## 5. RS-485 的处理

转接板上的 RS-485 是独立候选分支，不是 P2 CAN probe 的依赖，也不是 CAN 的第二节点。
在 P3 CAN Gate 关闭前只完成硬件识别和不带电 pinout 记录，不实现 Modbus RTU。

若后续单独开启 RS-485/Modbus RTU：先用 PTY 验证 CRC16、地址、寄存器、timeout 和帧间隔，
再验证 UART、DE/RE/自动方向、baud/parity、终端/偏置和断线；它使用独立 worker/adapter，
不能塞进 CAN I/O loop 或抽成通用 `Transport`。

## 6. 立即可做与等待项

### 板卡到货前

- 完成 P1-G0～G3 的代码审计、Release runner 修正和 clean evidence；
- 准备空白硬件 manifest、照片命名、kernel/DT 回滚清单；
- 不下载来历不明的 dtbo，不修改 boot，不编写假定 MCP2515 的 overlay。

### 到货当天需要用户提供/确认

1. 商品链接或完整商品名；
2. 包装标签、PCB 正面、PCB 背面和所有芯片近照；
3. 随板 pinout/拨码/跳帽说明；
4. 是否随货提供原理图、驱动、镜像或 DTO；
5. 当前手里是否已有第二个 CAN peer、CAN 收发器和两个 120 Ω 端接。

收到这些信息后，先关闭 P2-G0，再决定实际是 MCP2515/SPI3 路径还是改走其它驱动路径。
