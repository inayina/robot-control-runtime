# 闭环作品集冻结 — 证据

状态：**部分采集 / 未关闭**（第 1–10、12–13 项已入库；第 11 项 RS-485 掉线瞬间仍缺）  
Gate：[CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md](../../docs/plans/CLOSED_LOOP_PORTFOLIO_FREEZE_GATE.md)  
批次：[20260816T090900Z](20260816T090900Z/NOTES.md)（环境/CAN）、[20260816T091200Z](20260816T091200Z/NOTES.md)（Overview 成功态）

没有对应日志、candump、录屏或照片的项必须保持未跑。目录存在不等于整表通过。
外接 LED **不是**本表必要条件；不要把 Mock 或 requested 写成灯亮。

演示进程：Orange Pi `rcr_cell_app --can can0`（唯一 CAN owner）+ localhost
`rcr_modbus_rtu_agent` → `/dev/ttyS7`；ThinkPad Qt `--cell-peer`。不要并行写 `can0` 的 `rcrd`。
不要加 `--modbus-peer` 让 Qt 抢 agent。

红外极性：固件 `ACTIVE_HIGH`（挡片遮挡 = PA0 HIGH）。2026-08-16 成功态已观察到 bit0=1。

| # | 项 | 状态 | 证据来源 |
|---:|---|---|---|
| 1 | Orange Pi 环境 | pass | `20260816T090900Z/environment.txt`：`can2+` / `orangepi4pro` |
| 2 | `can0` 配置 | pass | `orangepi_can0.txt`：500 kbit/s，ERROR-ACTIVE，`restart-ms 0` |
| 3 | STM32 心跳/状态 | pass | `candump_021_041.txt`：session=11，约 100 ms |
| 4 | SG90 HOME → TARGET | pass（无录像） | 操作者下发 TARGET；成功态画面为到位，无运动录像 |
| 5 | PA0 实物边沿 | pass | Overview REACHED；CEL1 `input_bits=0x1`（挡片由操作者放置） |
| 6 | CAN `POSITION_REACHED` | pass | CEL1 bit0=1 与 Overview REACHED 同时成立 |
| 7 | Runtime/应用 CellReady | pass | Overview Cell Ready TRUE；CEL1 `cell_ready=1`，mode ACTIVE |
| 8 | Modbus 写 DO0 | pass | CEL1：requested=1 先于/等于 confirmed=1，status CONFIRMED |
| 9 | MR0 DO0 ON / 模块 | pass | Overview Output ON 且 Confirmed ON |
| 10 | Qt 端到端画面 | pass | `20260816T091200Z/qt_overview_cell_ready.png` |
| 11 | RS-485 掉线/恢复 | 未跑 | 操作者声明已插拔；无 OFFLINE 瞬间日志。CAN 插拔见 NOTES.md |
| 12 | commit SHA | pass | `c0d793bf7cd8e495fb61688fcac191965d7ca800` |
| 13 | dirty/clean | pass（dirty） | `git_dirty=true`；不得升 clean |

## 自动 vs 人工

| 种类 | 例子 | 能证明什么 |
|---|---|---|
| 自动 | Linux CTest、`test_cell_ready_mapper`、STM32 `test_logic`、Qt offscreen | 软件合同 |
| 人工/实物 | 上表 1–11 | 物理闭环；未采集 = 未跑 |

## 作品集素材（采集后放本目录或 `evidence/portfolio/`）

1. 架构图：仓库主图即可，不必另画。
2. Qt Overview 成功态截图：`20260816T091200Z/qt_overview_cell_ready.png`。
3. 45–90 s 演示录像：未采集。
4. CAN 插拔见 `20260816T090900Z/NOTES.md`；RS-485 掉线瞬间仍缺。
