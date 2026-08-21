# LD5-04 Modbus agent 不可用与 loopback timeout

## Symptom

Modbus agent 或对端不可达时，调用方应返回失败/timeout，不得静默切换到 Mock 或伪造设备状态。

## Facts

- 执行 `test_modbus_agent_loopback`，测试退出码为 `0`，1/1 test passed。
- 测试同时覆盖 mock RTU backend 的 localhost agent round-trip 和 `unconnected_client_does_not_silently_become_mock`。
- 分类为 `LOCAL / LOOPBACK / NO PHYSICAL RS485 / DIRTY`；未打开 `/dev/ttyS7`。

## Unknowns

- 真实 agent systemd restart、串口权限、RS-485 收发方向、站点 timeout 和物理断线未执行。
- loopback 中的 `PHYSICAL` backend 字段是服务模型名称，不是物理设备证据。

## Hypotheses

- 连接失败被保留为错误，调用方不会凭空生成设备 snapshot；恢复需要重新连接/Probe。

## Experiment

```bash
RCR_BUILD_DIR=build/ld2-qt-off ./linux/scripts/run_ld5_incidents.sh
```

精确 CTest 命令记录在 `04_modbus_unavailable/command.txt`。

## Evidence

- 原始目录：`evidence/ld5_incidents/20260818T135627Z/04_modbus_unavailable/`。
- CTest 输出：`100% tests passed, 0 tests failed out of 1`；退出码 `0`。
- 本证据明确标记 `NO PHYSICAL RS485`。

## Root Cause (only if proved)

对 unconnected fixture 已证明的原因是没有可连接的 agent server；没有真实 RS-485 故障根因可声明。

## Recovery

loopback 测试由 server 重新 listen 后建立新 client session；真实 agent/串口恢复留给后置 Gate，不能在本机自动模拟为 physical recovery。

## Fix (or No Code Change)

No Code Change。现有 client/server error boundary 已满足本机不可用语义演练。

## Regression

loopback round-trip、session read/write 和 unconnected timeout 均通过；未增加第二个串口 owner。

## Residual Risk

`/dev/ttyS7` 独占、真实 Modbus RTU CRC/timeout、RS-485 断线和 MR0 requested/confirmed 仍未验证。
