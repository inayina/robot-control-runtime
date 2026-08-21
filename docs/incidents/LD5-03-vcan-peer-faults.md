# LD5-03 VCAN 对端心跳、ACK 与重启故障

## Symptom

节点不再发送心跳、输出 ACK 超时或节点 session 重启时，Runtime 应观察到明确的 CommLoss、Hold/Fault 或 restart latch。

## Facts

- 使用 `vcan0`、`rcr_node_sim` 和现有 `rcr_fault_matrix`；没有 physical CAN。
- fault matrix 共 `22` 个 scenario，`pass=22 failed=0 permission_denied=0 unsupported=0 not_run=0`。
- 已通过的相关场景包括 `output_ack_timeout_hold`、`comm_loss_vcan`、`node_restart_vcan` 和 `sigterm_rcrd`。

## Unknowns

- `vcan0` 没有 CAN 收发器、电气错误计数、仲裁和物理断线语义。
- STM32 timeout、线缆断开和真实 ACK 延迟未执行。

## Hypotheses

- 模拟器故障注入可以覆盖 Runtime supervisor/state machine 的软件响应，但不能代表真实总线故障分布。

## Experiment

```bash
RCR_BUILD_DIR=build/ld2-qt-off ./linux/scripts/run_ld5_incidents.sh
```

脚本中的直接 fault-matrix 命令和返回码记录在 `03_vcan_faults/command.txt`。

## Evidence

- 原始目录：`evidence/ld5_incidents/20260818T135627Z/03_vcan_faults/`。
- `fault_matrix.txt` 保存 22 个 scenario 的结果；`stdout.txt` 保存运行输出。
- 场景退出码：`0`；LD5 总场景退出码：`0`。

## Root Cause (only if proved)

仅对本次模拟器注入可证明：CommLoss 来自模拟器停止/结束心跳，restart latch 来自模拟器 soft restart，ACK timeout 来自无 ACK 的合成测试条件；没有 Runtime 生产缺陷根因可声明。

## Recovery

fault matrix 内部按各场景停止 daemon、回收模拟器并验证退出；需要再次运行时重新创建 vcan peer 和 Runtime 进程，不重放旧命令。

## Fix (or No Code Change)

No Code Change。现有 fault matrix 已覆盖本机软件故障响应。

## Regression

22/22 通过；未改变 CAN V1 帧合同、NodeSupervisor ownership 或 Runtime C++。

## Residual Risk

physical CAN、STM32 session/ACK、bus-off、终端电阻和线缆断开仍是后置 Orange Pi/physical Gate。
