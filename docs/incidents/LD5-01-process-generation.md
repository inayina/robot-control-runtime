# LD5-01 Runtime 进程代际恢复

## Symptom

Runtime 进程被强制终止后，需要确认旧进程不会继续存在，替代进程能重新建立自己的运行代际。

## Facts

- 环境：ThinkPad `x86_64`，Linux `7.0.0-28-generic`，`build/ld2-qt-off`，git dirty。
- 分类：`LOCAL / VCAN / DIRTY`；使用现有 `vcan0` 和 `rcr_node_sim`，未操作 host systemd。
- 被注入的 `rcrd` 退出码为 `137`（SIGKILL）；替代进程 PID 为 `250780`，不同于被杀 PID `250768`，替代进程退出码为 `0`。

## Unknowns

- 本轮没有启动或重启真实 systemd unit，因此未知真实 systemd Restart policy、journal 和权限行为。
- 没有验证真实 CAN 节点或持久化外部状态下的恢复行为。

## Hypotheses

- 新 Runtime 进程应重新创建 mode、fault、session、mailbox、ACK 和 fd/thread 资源，不应继承旧命令。

## Experiment

```bash
RCR_BUILD_DIR=build/ld2-qt-off ./linux/scripts/run_ld5_incidents.sh
```

脚本中的精确命令与 PID 记录见 `01_process_generation/command.txt` 和 `lifecycle.txt`。

## Evidence

- 原始目录：`evidence/ld5_incidents/20260818T135627Z/01_process_generation/`。
- `killed_exit_code=137`、`replacement_exit_code=0`、`live_systemd_restart=not_run`。
- 总演练退出码：`0`。

## Root Cause (only if proved)

不适用：SIGKILL 是本次人为注入事件；没有生产故障根因可声明。

## Recovery

按脚本启动一个新的 `rcrd --duration-ms 300` 进程，确认它以新的 PID 正常退出；脚本随后只清理自己启动的模拟器 PID。

## Fix (or No Code Change)

No Code Change。现有进程边界和独立启动路径足以完成本机代际演练。

## Regression

脚本五个场景均通过；未修改 Runtime C++、CAN 合同或 systemd unit。

## Residual Risk

真实 systemd restart、真实服务用户权限、真实物理节点 session/ACK 重建仍未验证，留给后续授权 Gate。
