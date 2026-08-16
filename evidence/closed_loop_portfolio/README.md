# Closed-loop portfolio freeze — evidence scaffold

状态：**NOT RUN**（软件路径已在仓库；本目录没有实物采集日志）  
Gate：[CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md](../../docs/plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)

没有对应日志、candump、录屏或照片的项必须保持 NOT RUN。目录存在不等于 PASS。

演示进程：Orange Pi `rcr_cell_app --can can0`（唯一 CAN owner）+ localhost
`rcr_modbus_rtu_agent` → `/dev/ttyS7`；ThinkPad Qt `--cell-peer`。不要并行 `rcrd`。
15 项仍全部 NOT RUN，本文件不因软件接线改 PASS。

| # | 项 | 状态 | 说明 |
|---:|---|---|---|
| 1 | Orange Pi actual kernel / interface | NOT RUN | 需 `uname -a`、`ip -details link show can0`、是否 can2 内核 |
| 2 | CAN interface | NOT RUN | `can0` up、bitrate、错误计数 |
| 3 | STM32 peer | NOT RUN | heartbeat `0x021` / status `0x041` 稳定 |
| 4 | SG90 target movement | NOT RUN | HOME→TARGET 目视；不是角度闭环 |
| 5 | IR sensor real edge | NOT RUN | 挡片进出对射槽；先记录 PA0 raw 极性 |
| 6 | CAN POSITION_REACHED received | NOT RUN | `input_bits` bit0=1 的 NodeStatus |
| 7 | application CellReady transition | NOT RUN | Overview：false→true，mode=Active，online |
| 8 | Modbus FC05 DO0 | NOT RUN | requested 先于 confirmed；有界 timeout |
| 9 | MR0 relay state | NOT RUN | 线圈回读 / 继电器动作 |
| 10 | LED physical ON | NOT RUN | 现场灯；禁止把 Mock 或 requested 写成灯亮 |
| 11 | RS485 disconnect / recovery | NOT RUN | TIMEOUT/OFFLINE；Probe 后不重放旧 DO |
| 12 | commit SHA | NOT RUN | 采集时的 `git rev-parse HEAD` |
| 13 | dirty/clean 状态 | NOT RUN | `git status`；dirty-tree 不得升 clean |
| 14 | 哪些是自动测试 | 见下 | Linux/STM32 host CTest 是软件证据，不是本表 PASS |
| 15 | 哪些是人工目视 evidence | 1–11 | 红外、舵机、灯、拔线必须目视/抓帧 |

## 自动 vs 人工

| 种类 | 例子 | 能证明什么 |
|---|---|---|
| 自动 | `test_runtime_events`、`test_cell_ready_mapper`、STM32 `test_logic`、Qt offscreen | 软件合同 |
| 人工/实物 | 上表 1–11 | 物理闭环；未采集 = NOT RUN |

红外极性当前固件默认 `RCR_TARGET_SENSOR_POLARITY_UNSET`：未测量前不会上报到位。
