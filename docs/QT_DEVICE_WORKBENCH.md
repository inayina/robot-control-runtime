# Qt Device Test & Diagnostic Workbench

状态：**Phase 4 clean Gate passed；Phase 5A isolated Actuator MOCK local pass**

没学过 Qt：先读 [对照本仓的零基础笔记](QT_WORKBENCH_NOTES.md)，再看本文的构建/运行合同。
面试卡在 [KNOWLEDGE_BASE.md](KNOWLEDGE_BASE.md) §6.14 / §10.19 / Q18。

## Purpose

这是 `robot-control-runtime` 仓库内的本地机器人底层设备测试与诊断界面，不是 Web Robot Ops
Dashboard、ROS 2 HOC 或 CNC controller。当前最小壳包含：

- Overview：Runtime、backend/evidence、scheduler、device、heartbeat；
- Actuator 01：明确标记 `MOCK / ISOLATED` 的 Enable、Home、velocity、Jog、Normal/Quick Stop、
  soft-limit/tracking telemetry 和 Fault Reset；
- Tests：运行或取消现有 CAN Communication Health Test；
- Diagnostics：显示本次测试的 communication/device/test 诊断；
- Results：显示原子写入的 JSON/CSV 路径。

当前没有曲线、Direct CAN、Modbus、EtherCAT 或物理设备页面。Actuator 页面没有发送运动 CAN
帧，也不代表 Runtime admission 已实现。

## Architecture

```text
MainWindow (presentation only)
  ↓ signal / slot
WorkbenchController
  ├─ QTimer 100 ms → RuntimeApplicationAdapter::snapshot()
  ├─ QTimer 10 ms → MockActuatorProfile::tick(elapsed)
  ├─ QTimer 50 ms → bounded Jog lease renewal
  └─ QThread → HealthTestWorker
                 ├─ CanCommunicationHealthTest
                 └─ ResultWriter
  ↓
RuntimeApplicationAdapter
  ↓
RuntimeDaemon → LinuxRuntime / NodeSupervisor / CanIoLoop → SocketCAN
```

`MainWindow` 不拥有状态机、watchdog、fault、CAN fd、测试判定或文件写入。Controller 也不启动/
停止 Runtime；当前由 `main.cpp` composition root 组合 daemon、adapter、controller 和 window。

## Thread and time model

- UI thread：Qt event loop、widgets、100 ms snapshot timer；`snapshot()` 是快速、线程安全的读模型，
  不执行 CAN receive；
- Actuator Mock：仍在 UI thread，由 10 ms timer 显式推进；这是轻量模拟，不是 realtime control；
- Runtime scheduler/CAN I/O：仍由 RuntimeDaemon 自己拥有；
- Qt worker thread：同步 CAN Health 的采样等待和 ResultWriter `fsync`；完成后通过 queued signal
  把不可变结果副本交给 UI；
- Cancel：直接调用 TestRunner 的线程安全取消握手。不能排队到 worker event loop，因为同步测试
  运行时该 event loop 正被占用；
- deadline/heartbeat：`CLOCK_MONOTONIC`；run id 的墙钟毫秒只用于文件身份，不参与控制判断。

关闭顺序：停止 UI timer → 请求取消 → `QThread::quit()`/`wait()` → 销毁 controller/adapter →
`RuntimeDaemon::stop()`。测试最长由现有 timeout 约束，MainWindow 不负责设备 cleanup。

## Safety boundary

当前 Qt 与 Runtime 仍在同一进程，因此不能声称：

```text
Qt crash != Runtime crash
```

代码依赖方向已经允许未来用 IPC client 替换 Adapter 内部调用，但本 Phase 不设计 IPC 版本、
背压、重连或 lease。软件 `Cancel`/Stop 也不等于硬件 E-stop、STO 或功能安全。

## Build

Qt 默认关闭：

```bash
cmake -S linux -B build/qt-off \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=OFF \
  -DRCR_BUILD_TESTS=ON
cmake --build build/qt-off -j2
```

Qt6 开启：

```bash
sudo apt-get install qt6-base-dev
cmake -S linux -B build/qt-on \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON \
  -DRCR_BUILD_TESTS=ON
cmake --build build/qt-on -j2
```

## Run

先在一个终端启动当前明确标记为模拟的节点：

```bash
build/qt-on/rcr_node_sim --can vcan0 --node-id 1 --heartbeat-ms 20
```

再运行 UI：

```bash
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --results workbench-results
```

无显示服务器的纵向 smoke：

```bash
QT_QPA_PLATFORM=offscreen \
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --results /tmp/rcr-qt-results \
  --run-health-once
```

`--run-health-once` 仍走 Controller signal → worker → 同一个 headless CAN Health → ResultWriter；
它不是另一套测试实现。

Actuator Qt 纵向 smoke：

```bash
QT_QPA_PLATFORM=offscreen \
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --run-actuator-smoke-once
```

该路径通过 Controller 执行 Enable → Home → Start → Quick Stop，成功输出
`actuator_mock_smoke=pass ... evidence=MOCK`。它不发送 motion CAN frame。

## Current evidence boundary

- Headless Phase 3.5 clean evidence：`pass`；
- Qt OFF build/full CTest：`pass`，23/23；
- Qt6 6.4.2 ON build/full CTest：`pass`，23/23；
- Qt offscreen CAN Health：`pass`，结果为 `VCAN` / `SIMULATED`；
- clean commit：`834ec899b9aef0ef5c1b21b392456ec28fa1d5a7`；
- physical CAN / MCU / actuator：`not_run`。
- Phase 5A dirty-tree local：Qt OFF/ON 24/24，ASan/UBSan 6/6，Actuator smoke `pass`；
- Phase 5A clean evidence：`not_run`。

证据摘要见 [Qt Workbench Phase 4 Clean Evidence](../evidence/portfolio/qt_workbench_phase4_20260811.md)。

复现 Gate：

```bash
linux/scripts/run_qt_workbench_clean_evidence.sh vcan0
```
