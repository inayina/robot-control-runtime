# Evidence index

本目录保存可复现结果和脱敏摘要，不是功能列表，也不按目录名自动判定 PASS。证据字段与
状态词以 [EVIDENCE_SCHEMA.md](../docs/EVIDENCE_SCHEMA.md) 为准；realtime 结果另受
[REALTIME_EVIDENCE_SCHEMA.md](../docs/REALTIME_EVIDENCE_SCHEMA.md) 约束。

| Capability | Evidence area | What it can support |
|---|---|---|
| Runtime / CAN software path | `vcan_acceptance/`, `rcrd_acceptance/`, `fault_matrix/`, `sanitizer/` | vcan、进程、故障和本地代码验证 |
| Scheduler / realtime comparison | `thinkpad_baseline/`, `orangepi_baseline/`, `realtime_linux/` | 指定环境下的 latency 与调度对照 |
| Orange Pi | `orangepi/`, `orangepi_can_kernel/`, `systemd/` | 板上观察、构建/部署或内核分支的各自结果 |
| Orange Pi UART7 | `orangepi_uart7/` | can2 上 UART7 tty/驱动/占用与 CAN 回归；不是 physical RS-485 |
| STM32F103 physical CAN | `stm32f103_can/` | dirty-tree 双向 CAN V1、PC13 输出、SG90 无负载双位置目视动作与专用仲裁诊断；不是完整验收 |
| Workbench | `workbench/`, `qt_workbench/`, `portfolio/modbus_io_mock_gate_20260813.md`, `portfolio/remote_workbench_boundary_gate_20260813.md` | headless/Qt commissioning、Modbus Mock 与 Remote loopback 的受限验证；不是 physical RS-485 / PC–ARM |
| Closed-loop freeze | `closed_loop_portfolio/` | 15 项 closeout 脚手架；未采集项保持 NOT RUN |
| Experiments | `modbus_tcp/`, `ethercat_nic_gate/` | 独立实验或前置 Gate，不是 Runtime integration |
| Portfolio | `portfolio/` | 从原始证据提炼的可公开摘要 |

`orangepi`/`orangepi_baseline` 和 `workbench`/`qt_workbench` 是历史形成的不同采集入口。
为保留脚本引用和追溯路径，本轮不合并或重命名。

读取规则：

- 先读对应目录 README/manifest，再读时间戳批次；旧 P/RT/Phase 编号只是定位标签；
- 保留 `pass`、`failed`、`permission_denied`、`unsupported`、`not_run` 原分类；
- vcan、simulator、静态 unit verify 或普通 Linux 结果不能升级为 physical CAN；短样本和
  dirty-tree 实物结果只能按实际执行项标为 local physical smoke，不能升级为 clean hardware
  acceptance、真实执行器闭环、正式部署、硬实时或功能安全证明；
- 当前执行 Gate 见 [plans/README.md](../docs/plans/README.md)，不能由 evidence 目录名反推。
