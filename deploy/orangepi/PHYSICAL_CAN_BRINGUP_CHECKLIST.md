# Orange Pi Physical CAN 到货与 BSP 勾选表

状态：**Template / all NOT_RUN**  
关联：[V1 → Physical CAN 执行方案](../../docs/plans/V1_PHYSICAL_CAN_EXECUTION_PLAN.md)

复制本表到本地证据目录后填写。结果只允许 `PASS / FAILED / PERMISSION_DENIED /
UNSUPPORTED / NOT_RUN`。照片、内网地址、MAC、下载账号和私钥不直接提交公共仓库；公开摘要
只保存脱敏字段与文件 hash。

## 0. 元数据

| 字段 | observed | evidence | result |
|---|---|---|---|
| 日期/操作者 | | | NOT_RUN |
| git commit / dirty | | `git rev-parse HEAD`; `git status --short` | NOT_RUN |
| 板卡/SoC | | DT + PCB 丝印 | NOT_RUN |
| OS/内核/架构 | | `uname -srm` | NOT_RUN |
| kernel config 来源/hash | | `/proc/config.gz` 或 boot config | NOT_RUN |
| DTB/DTO 路径/hash | | `sha256sum` | NOT_RUN |

## 1. P2-G0 到货识别（首次不插 Orange Pi）

| id | 项 | expected / 问题 | observed | result |
|---|---|---|---|---|
| H0-01 | 商品身份 | Raspberry Pi 40-pin HAT；品牌、SKU、PCB revision、商品链接 | | NOT_RUN |
| H0-02 | 正反面照片 | 所有芯片、跳帽、端子和丝印清晰 | | NOT_RUN |
| H0-03 | CAN 控制器 | 是否确认为 MCP2515；若否写准确型号 | | NOT_RUN |
| H0-04 | CAN 收发器 | 型号、VCC、逻辑电平、standby/silent 脚 | | NOT_RUN |
| H0-05 | 晶振 | 准确频率及来源 | | NOT_RUN |
| H0-06 | SPI | SCLK/MOSI/MISO/CS 对应物理 pin | | NOT_RUN |
| H0-07 | IRQ | INT pin、GPIO、有效电平/边沿 | | NOT_RUN |
| H0-08 | 电源 | 3V3/5V 路径、是否有电平转换 | | NOT_RUN |
| H0-09 | 隔离 | CAN 隔离器、隔离电源、地连接要求 | | NOT_RUN |
| H0-10 | CAN 端接 | 120 Ω 是否板载、跳帽默认状态 | | NOT_RUN |
| H0-11 | RS-485 芯片 | UART/DE/RE/自动方向/隔离/终端偏置 | | NOT_RUN |
| H0-11A | 产品分支 | 普通版 UART/SP3485，或 (B) 版 SC16IS752/SPI，或其它 | | NOT_RUN |
| H0-12 | 原理图资料 | URL、版本、下载文件 hash | | NOT_RUN |
| H0-13 | 40-pin 兼容 | 按物理 pin 逐针核对；BCM/WiringPi 编号不得直接移植 | | NOT_RUN |
| H0-14 | 断电电阻检查 | 无明显电源短路；端接状态符合跳帽 | | NOT_RUN |

**H0 Stop**：控制器、晶振、CS、INT、电平或原理图任一关键项未知，不插板上电、不写 DTO。

## 2. P2-G1 内核与回滚

| id | 项 | expected | observed | result |
|---|---|---|---|---|
| K-01 | 原内核可启动 | 保存版本、modules、DTB 和 boot entry | | NOT_RUN |
| K-02 | 恢复方法 | 串口/显示器/备用介质或明确回退步骤 | | NOT_RUN |
| K-03 | CAN core | 运行内核启用 CAN/RAW/DEV | | NOT_RUN |
| K-04 | 目标驱动 | 与 H0 控制器一致；MCP2515 才用 mcp251x | | NOT_RUN |
| K-05 | SPI/GPIO/IRQ | 所需 controller 和内核机制已启用 | | NOT_RUN |
| K-06 | 独立启动项 | 新内核不覆盖唯一已知可启动项 | | NOT_RUN |
| K-07 | 基础回归 | SSH、存储、网络、systemd、时钟正常 | | NOT_RUN |
| K-08 | Runtime smoke | P1 Release binary 可启动到预期边界 | | NOT_RUN |

**K Stop**：没有验证回退路径，不安装/切换新内核；基础设备回归失败，不继续 CAN 调试。

## 3. P2-G2/G3 SPI、pinctrl 与 DTO

| id | 项 | expected | observed | result |
|---|---|---|---|---|
| D-01 | SPI controller | 运行 DT 映射出的准确 controller，不猜名称 | | NOT_RUN |
| D-02 | pinctrl | SCLK/MOSI/MISO/CS/INT 唯一且无冲突 | | NOT_RUN |
| D-03 | CS | 与实物和 DTO `reg` 一致 | | NOT_RUN |
| D-04 | IRQ | HAT INT 物理 pin 映射为 A733 GPIO；interrupt-parent/type 与实物一致 | | NOT_RUN |
| D-05 | clock | DTO oscillator 与 H0 证据一致 | | NOT_RUN |
| D-06 | compatible | 与真实控制器及当前内核 binding 一致 | | NOT_RUN |
| D-07 | supplies | regulator/supply 属性与电路一致 | | NOT_RUN |
| D-08 | DTO build | 保存 `.dts`、命令、`.dtbo` hash | | NOT_RUN |
| D-09 | 运行 DT | 反编译/运行节点与 DTO 源一致 | | NOT_RUN |
| D-10 | DTO 回滚 | 可移除并恢复原启动 | | NOT_RUN |
| D-11 | SPI/IRQ 观察 | 日志、设备节点、IRQ 计数符合预期 | | NOT_RUN |

`spidev` 若用于短期诊断，必须记录 bind/unbind；同一 CS 不允许同时占给 spidev 与 CAN 驱动。

## 4. P2-G4 controller probe / can0

| id | 项 | expected | observed | result |
|---|---|---|---|---|
| C-01 | 驱动 probe | dmesg 无未解释 probe/IRQ/SPI 错误 | | NOT_RUN |
| C-02 | SPI device | sysfs device 与预期 bus/CS/driver 一致 | | NOT_RUN |
| C-03 | netdev | `can0` 来自预期设备 | | NOT_RUN |
| C-04 | interface detail | 保存 driver、state、clock、restart、berr counters | | NOT_RUN |
| C-05 | bitrate | 与晶振/peer 一致，命令已归档 | | NOT_RUN |
| C-06 | up/down | 可重复操作，无内核异常 | | NOT_RUN |
| C-07 | error counters | before/after 均保存并解释 | | NOT_RUN |

建议保存但不预填结果：

```bash
dmesg --color=never
ip -details -statistics link show can0
find /sys/bus/spi/devices -maxdepth 2 -type l -o -type f
cat /proc/interrupts
```

## 5. P2-G5 loopback / peer

| id | 项 | expected | observed | result |
|---|---|---|---|---|
| T-01 | internal loopback | 驱动支持则收发；不支持记 UNSUPPORTED | | NOT_RUN |
| T-02 | peer 身份 | 第二 CAN 接口或 MCU 的准确型号/固件 hash | | NOT_RUN |
| T-03 | topology | 两节点、双绞线、两端各 120 Ω | | NOT_RUN |
| T-04 | 断电总线电阻 | CANH-CANL 接近 60 Ω，记录仪表误差 | | NOT_RUN |
| T-05 | bitrate | 两端配置完全一致 | | NOT_RUN |
| T-06 | peer → Orange Pi | `candump` 收到已知 frame | | NOT_RUN |
| T-07 | Orange Pi → peer | peer 收到 `cansend` frame | | NOT_RUN |
| T-08 | counters | 正常路径无未解释 error/bus-off 增长 | | NOT_RUN |
| T-09 | 无 peer ACK 对照 | 行为按控制器/counter 事实记录，不写 PASS | | NOT_RUN |
| T-10 | 重复上电 | reboot 后可重复 probe/up/peer 通信 | | NOT_RUN |

## 6. P2 关闭审核

- [ ] H0 身份、电气和资料齐全；没有用商品标题猜芯片。
- [ ] 已区分普通版、(B) 版或其它版本；没有复制 Raspberry Pi BCM GPIO/overlay。
- [ ] 新内核与 DTO 均有可验证回滚路径。
- [ ] `can0` 的 controller、driver、clock、CS、IRQ 可追溯。
- [ ] internal loopback 与两节点物理收发被分开举证。
- [ ] RS-485 未被当作 CAN peer 或 Runtime CAN 证据。
- [ ] 没有声称硬实时、功能安全、MCU 业务协议或 `rcrd` 真总线已经完成。
