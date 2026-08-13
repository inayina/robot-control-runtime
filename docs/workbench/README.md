# Device Test & Diagnostic Workbench

本仓内的本地设备测试 / bring-up / 诊断工作台。不新建仓库，也不把 Runtime 搬出去。

不是 Web Dashboard、ROS 2 HOC、CNC controller，也不是硬件安全回路。

**状态**：Phase 1–4 已关。Phase 5A Actuator Mock 只有 dirty-tree local。当前 Gate 是
Modbus I/O `MOCK / NO PHYSICAL RS485`；A2 Runtime admission、实物执行器、真实 RTU、IPC、
Direct CAN 都未做。

还开着的门、停止规则：[GATES.md](GATES.md)。  
没学过 Qt：[NOTES.md](NOTES.md)。  
Actuator 状态机细节：[ACTUATOR.md](ACTUATOR.md)。  
阶段流水账（不是当前合同）：[archive/PHASE_HISTORY.md](archive/PHASE_HISTORY.md)。

## 分层（按这个读源码，不要按五层一横拆）

```text
ui/MainWindow                 只展示，不判定
        │ signal / slot
app/main.cpp                  组装 daemon / adapter / controller / window
controller/                   用例：拉 snapshot、跑测试、推进 Mock
        │
services/                     TestRunner / CAN Health / ResultWriter
application/                  DTO + RuntimeApplicationAdapter
profile/                      Mock 执行器 / Modbus I/O（隔离，不进 Runtime/CAN/Serial）
        │
RuntimeDaemon                 唯一状态机 / watchdog / CAN fd owner
        │
SocketCAN → vcan0 → rcr_node_sim
```

| 层 | 目录 |
|---|---|
| UI | `linux/tools/qt_device_workbench/ui/` |
| 组装 | `linux/tools/qt_device_workbench/app/` |
| Controller | `linux/tools/qt_device_workbench/controller/` |
| Services | `linux/{include/rcr,src}/workbench/services/` |
| Adapter / DTO | `linux/{include/rcr,src}/workbench/application/` |
| Mock profile | `linux/{include/rcr,src}/workbench/profile/` |
| 测试 | `linux/tests/workbench/` |
| Runtime | 既有 `linux/src/{daemon,runtime,...}`，不因本工具再拆 |

include 例：`rcr/workbench/services/test_runner.hpp`。namespace 仍是 `rcr::workbench`。
五层一横只管 `rcrd` / SocketCAN / 部署，见 [ARCHITECTURE.md](../ARCHITECTURE.md)。

## 现在界面上有什么

- Overview：Runtime/fault/interlock、backend/evidence、CAN 计数、device session、heartbeat、ACK
- Actuator 01：`MOCK / ISOLATED` 的 Enable / Home / Jog / Stop / Fault Reset
- Modbus I/O：`MOCK / NO PHYSICAL RS485` 的 slave scan、4 DI injection、4 DO request/reply
- Tests：跑或取消 CAN Communication Health
- Diagnostics：本次测试的 communication / device / test 事件
- Results：原子写入的 JSON/CSV 路径

没有：曲线、CAN Monitor、Direct CAN、真实 Modbus RTU、EtherCAT、物理设备页。
Actuator 页不发运动 CAN 帧。

### Modbus I/O 当前做了什么

```text
Modbus I/O QWidget
        ↓ signal / slot
WorkbenchController
        ↓
MockModbusIoProfile
```

已实现：确定性 Mock slave 1/2/3 的 ONLINE/TIMEOUT、4 DI 显式 injection、4 DO 的 requested /
confirmed 分离、All OFF、success/timeout/exception/rejected 和恢复。失败 reply 不会把 requested
值伪装成 confirmed。Controller 在下一轮 event loop 完成 Mock scan；MainWindow 没有 scan loop。

未实现：串口枚举、QSerialPort/QtSerialBus/libmodbus、RTU frame/CRC、MR0-IOR08 register map、
真实 DI、继电器 DO、physical timeout/reconnect。`9600 / None / Slave 1` 都是页面 placeholder，
不是厂商默认值证据。

## 怎么编、怎么跑

Qt 默认关，core 不查找 Qt：

```bash
cmake -S linux -B build/qt-off -DRCR_BUILD_QT_DEVICE_WORKBENCH=OFF -DRCR_BUILD_TESTS=ON
cmake --build build/qt-off -j2
ctest --test-dir build/qt-off -R 'workbench|mock_actuator' --output-on-failure
```

开 Qt6（不要用 Qt5 凑合）：

```bash
sudo apt-get install qt6-base-dev
cmake -S linux -B build/qt-on -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON -DRCR_BUILD_TESTS=ON
cmake --build build/qt-on -j2
```

本机 `vcan0`（不要拿到 Orange Pi 上套）：

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
```

有窗口：

```bash
build/qt-on/rcr_node_sim --can vcan0 --node-id 1 --heartbeat-ms 20
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --evidence vcan --results workbench-results
```

无显示器，仍走同一条 Controller → worker → Health → ResultWriter：

```bash
QT_QPA_PLATFORM=offscreen \
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --evidence vcan \
  --results /tmp/rcr-qt-results --run-health-once
```

Actuator Mock smoke（不发 motion CAN）：

```bash
QT_QPA_PLATFORM=offscreen \
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --evidence vcan --run-actuator-smoke-once
```

`--evidence` 必填，只接受 `vcan` 或 `physical`。它不根据接口名猜证据等级。当前代码允许把
`physical` 显式传给同一条只读 CAN Health 链，例如未来在实际拥有 `can0` 的主机上运行：

```bash
QT_QPA_PLATFORM=offscreen \
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can can0 --node-id 1 --evidence physical \
  --results /tmp/rcr-qt-physical --run-health-once
```

这条 physical Qt 命令目前是**可执行入口，不是已通过证据**；尚未在 Orange Pi 上安装 Qt6
并运行。Health 仍只读取 Runtime snapshot，不打开第二个 CAN socket，也不发送舵机运动命令。

## 线程（一句话）

UI 线程：`QTimer` 100 ms 拉已经算好的 snapshot；Actuator Mock 用 10 ms timer 显式 `tick`，
Modbus Mock 的 scan 用 queued completion，二者都不是实时环或真实 I/O。
Worker `QThread`：同步 CAN Health 和 `fsync`。Cancel 必须直接打 `TestRunner`，因为
`run()` 占着 worker 的 event loop。deadline 用 `CLOCK_MONOTONIC`；墙钟只给 run id / 文件名。

关闭：停 timer → cancel → `QThread::quit/wait` → 拆 adapter → `RuntimeDaemon::stop()`。

当前 Qt 和 Runtime 同进程，**不能**说 `Qt crash != Runtime crash`。

## 证据

| 项 | 状态 |
|---|---|
| Headless Phase 3.5（`cf5892e`） | pass，[摘要](../../evidence/portfolio/workbench_phase3_5_20260811.md) |
| Qt Phase 4 offscreen VCAN（`834ec899`） | pass，[摘要](../../evidence/portfolio/qt_workbench_phase4_20260811.md) |
| Phase 5A Mock local | 旧 Qt OFF/ON 24/24，ASan 6/6，smoke pass；**不是** clean Gate |
| current Qt hardening local | Qt OFF 22 pass + 2 skip；Qt ON 23 pass + 2 skip；QtTest 4 场 pass |
| Modbus I/O Mock local | headless profile + Qt presentation 已实现；fresh 总数见当前 Gate 报告，非 physical RS-485 |
| physical Qt Health / 伺服控制 | not_run / not implemented |
| 人工盯窗口 | 未做 |

当前 QtTest 在 ASan/UBSan 构建下也通过（`detect_leaks=0`）；完整 LeakSanitizer 在当前 ptrace
运行环境中自身 fatal，记 `unsupported`，不能写成泄漏检查 PASS。两项 skip 都因为本机没有
`vcan0`，不是 VCAN 通过证据。

复现 Phase 4：`linux/scripts/run_qt_workbench_clean_evidence.sh vcan0`

## 能说 / 不能说

可以：无 Qt 的 TestRunner + 只读 CAN Health + 原子 JSON/CSV；可选 Qt 接到同一条链；
Actuator 01 和 Modbus I/O 都是隔离 Mock。

不能：Qt physical Health、实物 actuator 或 RS-485/Modbus RTU 已验证；Qt 崩了 Runtime 一定
活着；数字输出 mailbox 已经在做 Jog；五层一横覆盖了 Workbench 目录。仓库已有的 STM32
双向物理 CAN、PC13、SG90 双位置目视动作和仲裁诊断是独立 evidence，不会自动升级
Qt/Workbench/Modbus Gate。

面试仍走 [KNOWLEDGE_BASE.md](../KNOWLEDGE_BASE.md) §6.14 / §10.17–10.19 和
[模块卡 36–42](../MODULE_KNOWLEDGE_CARDS.md)。
