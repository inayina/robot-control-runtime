# Portfolio evidence index

对外叙事与简历口述入口：[docs/portfolio/README.md](../../docs/portfolio/README.md)
（主文 [SYSTEMS_SOFTWARE_PORTFOLIO.md](../../docs/portfolio/SYSTEMS_SOFTWARE_PORTFOLIO.md)）。

本目录只保存适合公开仓库的脱敏摘要。它们来自本地原始证据，但不会把局域网 IP、MAC、
私钥路径或全部 lateness 样本提交到作品集。

每份摘要必须读取其自己的 commit、`git_dirty`、环境和边界；本目录同时包含 2026-08-05 dirty
bring-up、后续 clean Workbench 证据和 local/dirty Gate 收口，不能把一个批次的强度推广到
另一个批次。clean 复跑候选见
[`docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md`](../../docs/plans/PORTFOLIO_V1_RELEASE_PLAN.md)。
调度矩阵摘要已由 RT0 重分类为 **pilot**（见
[`docs/REALTIME_EVIDENCE_SCHEMA.md`](../../docs/REALTIME_EVIDENCE_SCHEMA.md)），不得当作
Real-time Lab 正式基线或与 RT1 计算提升百分比。

| 文件 | 覆盖 | 边界 |
|---|---|---|
| [`orangepi_bringup_20260805.md`](orangepi_bringup_20260805.md) | B0–B2 主机、构建、安装与内核能力 | daemon 未常驻 |
| [`orangepi_scheduler_20260805.txt`](orangepi_scheduler_20260805.txt) | B3 12 格总表与四个代表格；**RT0 pilot** | 空 callback；5 秒 Debug；dirty；非 RT1 |
| [`orangepi_rt1_smoke_20260805.md`](orangepi_rt1_smoke_20260805.md) | RT1 60s smoke 10/10；机制生效审阅 | dirty Release；非正式 30min baseline |
| [`orangepi_rt2_cyclictest_20260805.md`](orangepi_rt2_cyclictest_20260805.md) | RT2 四代表 cyclictest 与同核 OTHER 归因 | dirty；timerlat/perf unsupported |
| [`rt3_userspace_thinkpad_20260805.md`](rt3_userspace_thinkpad_20260805.md) | RT3 本机开发对照（含 PI 数值） | **非** Orange Pi 主证据 |
| [`orangepi_rt3_userspace_20260805.md`](orangepi_rt3_userspace_20260805.md) | RT3 板上主证据（5/5，含 PI） | dirty experiment；未并入 Runtime |
| [`orangepi_rt4_gate_20260805.md`](orangepi_rt4_gate_20260805.md) | RT4 PREEMPT_RT Gate：**Blocked + Fallback** | 禁止装核；见 `docs/PREEMPT_RT_FEASIBILITY_GATE.md` |
| [`orangepi_rt6_segmented_20260805.md`](orangepi_rt6_segmented_20260805.md) | RT6 分段时延软件 peer（4/4） | dirty experiment；非 CAN e2e；未并入 Runtime |
| [`orangepi_rt7_wrapup_20260805.md`](orangepi_rt7_wrapup_20260805.md) | RT7 Lab 收口：因果图、证据等级、负面结果 | 无板上 PREEMPT_RT 对照；非 clean formal |
| [`figures/`](figures/) | 作品集配图（拓扑 / 分层 / RT1 / RT6） | 由 dirty 证据数字生成；脚注边界仍有效 |
| [`modbus_tcp_lan_20260805.md`](modbus_tcp_lan_20260805.md) | Orange Pi client → ThinkPad server | Wi-Fi demo；非现场设备/Runtime |
| [`workbench_phase3_5_20260811.md`](workbench_phase3_5_20260811.md) | headless Workbench adapter/test/result chain | clean；不是 Qt/physical device |
| [`qt_workbench_phase4_20260811.md`](qt_workbench_phase4_20260811.md) | Qt offscreen vcan Health chain | clean；不是 physical CAN/crash isolation |
| [`modbus_io_mock_gate_20260813.md`](modbus_io_mock_gate_20260813.md) | Modbus I/O Mock Gate Qt OFF/ON closure | local/dirty；`MOCK / NO PHYSICAL RS485` |
| [`remote_workbench_boundary_gate_20260813.md`](remote_workbench_boundary_gate_20260813.md) | Remote Workbench Boundary Gate Qt OFF/ON closure | local/dirty；`LOOPBACK / NO PHYSICAL PC-ARM`；无 UDP |

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
