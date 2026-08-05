# Portfolio evidence index

本目录只保存适合公开仓库的脱敏摘要。它们来自本地原始证据，但不会把局域网 IP、MAC、
私钥路径或全部 lateness 样本提交到作品集。

当前摘要均来自 `git_dirty=true` 的 2026-08-05 bring-up，只能证明当时本地观察，**不是**
clean-commit 正式基线。clean 复跑 Gate 见
[`docs/PORTFOLIO_V1_RELEASE_PLAN.md`](../../docs/PORTFOLIO_V1_RELEASE_PLAN.md)。

| 文件 | 覆盖 | 边界 |
|---|---|---|
| [`orangepi_bringup_20260805.md`](orangepi_bringup_20260805.md) | B0–B2 主机、构建、安装与内核能力 | daemon 未常驻 |
| [`orangepi_scheduler_20260805.txt`](orangepi_scheduler_20260805.txt) | B3 12 格总表与四个代表格 | 空 callback；5 秒 Debug；dirty |
| [`modbus_tcp_lan_20260805.md`](modbus_tcp_lan_20260805.md) | Orange Pi client → ThinkPad server | Wi-Fi demo；非现场设备/Runtime |

原始本地路径：

- `evidence/orangepi/20260805T084028Z/`
- `evidence/orangepi_baseline/20260805T085844Z/`
- `evidence/modbus_tcp/dual_host_20260805T091018Z.txt`

本次整理时的 SHA-256（用于确认本地原文没有被静默替换）：

```text
9a2cab16c561aee8b6bfc5c64b3ff55fd8f06541732f41acbab77bd9cb73a1e1  BRINGUP_FILLED.md
af0357054a4deb36f65e10430f623b262d4b6be0401174a42f3eed5ea5f78749  b2_bringup_report.txt
e9cef8ad18a5245da9bb9f3cd1d3c91e026fe98d0d2bd457875886a468bb721f  B3_SUMMARY.txt
d20712dec82920b84c953ef5b8cbe5ac083d8030b5b058222f3791c5e5adb801  B3_environment.txt
e4ea5b6826c078765950a373c9b6a4079cb3847793284a1022019b356d3736b6  modbus_tcp_dual_host.txt
```
