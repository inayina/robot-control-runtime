# Modbus I/O Mock Gate local verification — 2026-08-13

状态：`pass`（Gate local implementation verification）  
证据等级：`MOCK / NO PHYSICAL RS485`  
源码基线：`0a0e95064e39d966b9eda95ba59925086011c8fd` + 未提交的关闭测试/文档  
`git_dirty=true`

## 验证结果

| 配置 | fresh build | CTest | vcan 结果 |
|---|---|---|---|
| `RCR_BUILD_QT_DEVICE_WORKBENCH=OFF` | pass | 25/25 pass | 宿主机复跑 SocketCAN/Workbench vcan tests pass |
| `RCR_BUILD_QT_DEVICE_WORKBENCH=ON` | pass | 26/26 pass | 宿主机复跑 SocketCAN/Workbench vcan tests pass |

QtTest 使用 offscreen platform 通过。初次沙箱 CTest 因看不到宿主机 `vcan0`，两项 vcan 用例
被正确标为 skip；随后对同一 fresh build 在宿主 SocketCAN 环境重跑，所有测试通过。因此最终
vcan 结论来自宿主复跑，不把沙箱 skip 计为 pass。

## 本 Gate 覆盖

- scan：SCANNING、ONLINE/TIMEOUT 结果、非法 completion 进入 ERROR、显式新 scan 恢复；
- DI：4 channel 的显式 Mock injection；
- DO：requested/confirmed 分离，success、timeout、exception、rejected；
- failure：失败不改 confirmed state，后续显式命令可恢复；
- controls：All OFF 与 invalid channel；
- Qt：页面标签、signal/slot、snapshot/reply propagation 和 offscreen UI test；
- build boundary：Qt OFF 不依赖 Qt/SerialBus/SerialPort。

## 不能由本结果证明

- clean-commit release evidence；
- QSerialPort、Qt SerialBus、libmodbus 或真实 Modbus RTU；
- CRC16、MR0-IOR08 register map、实际 slave response；
- 24 V DI、relay DO、physical RS-485、接线/终端/偏置；
- Orange Pi 上的 Qt、远程 TCP/UDP Workbench 或 EtherCAT；
- physical CAN remaining acceptance。

本摘要关闭的只是
[`MODBUS_IO_MOCK_GATE.md`](../../docs/plans/MODBUS_IO_MOCK_GATE.md)。下一 Gate 未选择。
