# STM32F103 physical CAN evidence

本目录发布 STM32F103C8T6 + SN65HVD230 作为 Orange Pi MCP2515 `can0` peer 的脱敏摘要。
原始终端交互、临时固件备份和带主机细节的文件不提交仓库。

## 2026-08-13 physical protocol smoke

状态：**G2 PASS / G3 PASS / G4 protocol smoke PASS；完整 hardware acceptance PARTIAL**  
classification：`pass`（仅下列已执行项）  
采集时间：2026-08-13T03:54:42Z  
仓库基线：`8d2998f5b50349131ecf4532568ae23f9004e2f1`，**dirty implementation tree**

### 环境

| 项 | 实际观察 |
|---|---|
| MCU | Device ID `0x410`，STM32F101/F102/F103 Medium-density，64 KiB Flash，Cortex-M3 |
| ST-Link | ST-Link/V2，FW `V2J47S7`，SWD 4 MHz，目标 3.25 V |
| programmer | STM32CubeProgrammer CLI 2.22.0（STM32CubeIDE 2.1.1 bundled） |
| 固件 | 3268 bytes；sha256 `ad0a64327324d81a0d7d78a22924a90e9e0d9432d571f6fed146bb33a605abda` |
| Orange Pi | `6.6.98-sun60iw2-can2+`，MCP2515 `spi3.0` → `can0`，500 kbit/s |
| MCU peer | STM32F103 bxCAN + 3.3 V SN65HVD230，node 1 |

### G2：烧录、启动与 session

- 写入前读取了原 64 KiB Flash 临时恢复点，sha256
  `a4aea0ffbee5fe546205265655d3817d2e3070a3c51be3134664e76a2ff1dea3`；该文件位于 `/tmp`，
  不是持久仓库资产；
- CubeProgrammer 只擦除 internal sectors 0–3，download、verify、software reset 成功；
- HotPlug 读回向量 SP=`0x20005000`、Reset=`0x080008C1`，与 ELF 一致；
- session journal 首记录为 `FFFE0001`，即 value=1、complement=`0xFFFE`；
- CubeProgrammer 报 `Core is running`。

结论：烧录/启动/session smoke **PASS**。没有示波器或 HSE 频率实测，不能把寄存器设计值
写成晶振精度证据。

### G3：STM32 → Orange Pi

约 1.2 s 抓包中，heartbeat/status 每 100 ms 成对出现：

```text
021#01000001000102AB
041#0101000100000000
...
021#01000001000102B6
041#0101000100000000
```

protocol=1、boot=1、session=1，heartbeat sequence 连续；status 为 interlock ready、
input=0、fault=NONE。`can0` 保持 ERROR-ACTIVE，所有 error-warning/passive/bus-off 计数为 0。

结论：真实 CANH/CANL 上 STM32→Orange Pi 周期帧 smoke **PASS**。

### G4：Orange Pi → STM32

```text
TX 061#010100010001011E
RX 081#0100000100010100  # APPLIED, mirror=1

RX 081#0101000100010000  # lease 后重复 seq → STALE_SEQUENCE, mirror=0
RX 081#0102000100020000  # 错误 session → SESSION_MISMATCH, mirror=0
```

最终 Orange Pi TX=3，收发错误和 bus-off 均为 0。协议状态证明 lease 后 mirror 归零，且拒绝
没有恢复输出。

随后按当前 `session=2` 约 30 s 连续刷新合法命令，保存到 sequence `0x0016..0x004E` 的连续
`APPLIED + mirror=1`，用户同时目视确认 PC13 LED 点亮。这支持“CAN 命令到 PC13 输出链通过”
的本地 operator-observed smoke；没有照片或电气波形，不能写成保存的 LED 电平测量。

同日已在 dirty tree 实现 PA8/TIM1_CH1 50 Hz 双位置 PWM，主机逻辑测试和 ARM 交叉构建通过；
3460-byte BIN（sha256 `c7050c1af4e7ff7958dfab24989e4e16c22560a0bcc8a655582e9b4cfa4dd9c8`）
已由 CubeProgrammer 2.22.0 download/verify/reset。复位后观察到新 boot/session=3、连续约 100 ms
heartbeat/status，`can0` 为 ERROR-ACTIVE 且错误计数为 0。没有发送 A/B 命令，因此该结果只
证明新固件静态启动和 CAN 周期上报，不是 PA8 波形或 SG90 动作证据。

用户明确允许动作后，session=3 下执行一次受控 A→B 命令：sequence 1–30 以 100 ms 刷新
位置 A，全部返回 `APPLIED + mirror=0`；停 1 s 后 sequence 31–60 刷新位置 B，全部返回
`APPLIED + mirror=1`。停止刷新后再次发送 sequence 60，返回：

```text
081#01010003003C0000  # STALE_SEQUENCE, session=3, mirror=0
```

随后 `can0` 仍为 ERROR-ACTIVE，bus-errors/error-warning/error-passive/bus-off 均为 0。该切片
支持 A/B 命令与 lease 后逻辑归零；首次目视只看到一次转动，无法排除初始位置已在 A，
因此当时不把它单独标为双位置动作 PASS。

为排除初始位置恰好接近 A 的歧义，又以 sequence 61–120 执行 B→A→B：三段分别全部返回
`mirror=1/0/1` 的 `APPLIED`。用户目视确认中间出现两次方向相反的转动。因此可以把“无负载
双位置目视动作”标为 **operator-observed smoke PASS**。没有照片、位置传感器或 PA8 波形，
不能据此声称精确角度、位置闭环、1.25/1.75 ms 实测或故障关闭时序通过。

### 物理仲裁竞争：bxCAN 低优先级发送者

状态：**PASS（dirty-tree 专用诊断固件）**。

为避免把普通周期通信误写成“测过仲裁”，另行烧录一次性诊断固件：STM32 使用标准帧
`0x7FE`、1 ms 尝试周期和 bxCAN `NART=1`，Orange Pi MCP2515 同时以标准帧 `0x001`、
1 ms 用户态节拍竞争。诊断固件直接读取 bxCAN mailbox 0 的 `TXOK0`、`ALST0`、`TERR0`，
30 s 后由 ST-Link HotPlug 读取 SRAM 计数：

```text
attempts=30000
tx_ok=29963
arbitration_lost=37
tx_error=0
```

`29963 + 37 + 0 = 30000`，计数守恒；同一轮 Orange Pi 错误帧捕获为 0，测试前后
`bus-errors/arbit-lost/error-warn/error-pass/bus-off` 均为 0。该结果证明至少 37 次 STM32
发送尝试在真实 CAN 总线上输给更低 ID，并由控制器无破坏地退出；它不证明两个节点严格
同时起帧、位级波形、最坏仲裁延迟、错误计数阶梯或 bus-off 恢复。

诊断 BIN sha256 为
`7771fd87fee985d3341d188c45da4a556ec1349b52191980ae3b50947a8f9528`。测试结束后已重新烧录并
verify 正常 CAN/SG90 固件（sha256
`c7050c1af4e7ff7958dfab24989e4e16c22560a0bcc8a655582e9b4cfa4dd9c8`）；复位后重新观察到
连续 `0x021` heartbeat 与 `0x041` status，`can0` 为 ERROR-ACTIVE。

### 尚未运行

- PC13/PA8 的示波器或逻辑分析仪电平与 PWM 周期/脉宽；
- SG90 供电跌落和停止脉冲的电气时间线；
- CANH/CANL 断线、重连、MCU reset 后新 session、bus-off/ABOM；
- IWDG 主循环卡死复位时间；
- `rcrd --can can0` lifecycle、single-inflight ACK 与完整 physical fault matrix；
- clean commit 上的同提交重构建、重烧录与正式 evidence。
