# Qt 与本仓 Device Workbench 笔记（零基础对照源码）

状态：Living  
证据等级（先看这个，再往下读）：

| 你现在能说什么 | 等级 | 依据 |
|---|---|---|
| Qt 是什么、event loop / signal-slot / Widgets 在本仓怎么用 | **理解过** | 本文 + 四个源文件 |
| 可选 Qt6 target 已接到既有 headless 测试链 | **代码中使用过** | `linux/tools/qt_device_workbench/` |
| 无窗口 offscreen 跑通同一条 CAN Health | **测过（VCAN 软件）** | Phase 4 clean Gate，`834ec899` |
| 人工盯着窗口点按钮的视觉验收 | **未做** | 不能把 offscreen 说成 UI 已验收 |
| Qt 崩溃后 Runtime 一定还活着 | **未实现** | 当前同进程；没有 IPC |

配套文档：

- 分层阅读入口：[README.md](README.md)
- 还开着的门：[GATES.md](GATES.md)
- 面试卡：[KNOWLEDGE_BASE.md](../KNOWLEDGE_BASE.md) §6.14 / §10.19
- 阶段流水账（不是当前合同）：[archive/PHASE_HISTORY.md](archive/PHASE_HISTORY.md)

本文**不是** Qt 语法大全，也不教你做通用桌面 App。目标只有一个：没学过 Qt 的人，能对着本仓
四个文件讲清“界面怎么接到 Runtime，为什么不能把测试写进按钮函数”。

## 给初学者的一句话

Qt 在本仓只是**用户态显示器和按钮**：它按自己的节奏刷新已经算好的快照，并把“跑一次测试”
这件事交给后台线程。它不拥有 CAN socket、不拥有 watchdog、也不做 PASS/FAIL 判定。

## 建议阅读顺序

```text
① 本文 §1–§6（建立地图，先不要打开 Qt 官方教程）
     ↓
② 四个源文件，按这个顺序读（目录即分层；文件头和关键约束处有中文注释）：
     app/main.cpp
       → controller/workbench_controller.hpp/.cpp
       → ui/main_window.hpp/.cpp
       → controller/qt_metatypes.hpp
     ↓
③ 已经存在、且不依赖 Qt 的下游：
     application/RuntimeApplicationAdapter
       → services/CanCommunicationHealthTest
       → services/ResultWriter
       → profile/MockActuatorProfile（隔离 Mock，不是 Runtime）
     ↓
④ 动手：先 Qt OFF 证明 core 不依赖 Qt，再按需 Qt ON / offscreen
     ↓
⑤ 知识库 §6.14、§10.19 和面试 Q18
```

不要先学 QML、Qt Quick、Qt Charts、Model/View 框架或 Designer。本仓第一版 UI 只用
Widgets、几个 Label/Button/Table、一个 `QTimer` 和一个 `QThread`。

## 1. Qt 在本仓解决什么、不解决什么

没有界面时，工程师已经能用 headless 路径回答：

> Runtime 和模拟节点的通信健康吗？失败证据在哪个 JSON 里？

Qt 只多做一件事：把同一条路径变成可点的 Overview / Tests / Diagnostics / Results。
判定、采样、写文件仍然是 Phase 1–3 已经测过的 C++ 对象。

```text
你看见的窗口（Qt Widgets）
        │ 只显示、只发请求
        ▼
WorkbenchController
        │ 快读 snapshot；慢测试丢给 worker
        ▼
已经存在的 headless 服务
  Adapter / TestRunner / CAN Health / ResultWriter
        ▼
RuntimeDaemon（唯一 CAN fd / 状态机 / watchdog owner）
```

因此：

- 学会本仓 Qt，不等于学会“用 Qt 做机器人控制器”；
- 窗口能刷新，不等于控制闭环在 UI 里；
- 点 Run 能出 PASS/FAIL，不等于 Qt 自己会测 CAN。

## 2. 术语地图（第一次出现时的中文直觉）

| 中文直觉 | 英文 / Qt 名 | 在本仓落到哪里 |
|---|---|---|
| 应用程序对象，拥有整场 UI 生命 | `QApplication` | `main.cpp` 里创建，`app.exec()` 开始转圈 |
| 事件循环：一件一件处理鼠标、绘制、定时器 | event loop | UI 线程；被同步测试堵住时窗口会假死 |
| 能发信号、能进事件循环的对象基类 | `QObject` | Controller、Worker、多数控件 |
| “这件事发生了” | signal | 按钮 `clicked`、Controller `snapshotReady` |
| “听到以后做什么” | slot | `MainWindow::updateSnapshot`、`startHealth` |
| 谁创建、在哪个线程用 | thread affinity | widget 只在 UI 线程；worker 被 `moveToThread` |
| 跨线程时先排队、到对方循环里再执行 | queued connection | `healthRequested` → `runHealth` |
| 同线程立刻调用 | direct connection | 默认；UI 内部常用 |
| 桌面控件工具包 | Widgets | `QMainWindow`、`QLabel`、`QPushButton`、`QTableWidget` |
| 声明式界面语言 | QML | **本仓不用** |
| 定时器，到期后往 event loop 投一票 | `QTimer` | Controller 里 100 ms 拉 snapshot |
| 额外线程 | `QThread` | 只给会等待的 CAN Health + 写文件 |
| Qt 字符串 | `QString` | 仅 UI 适配层；核心 DTO 仍是 `std::string` |
| 让自定义类型能进 queued signal | metatype | `qt_metatypes.hpp` |

如果只记四句：

1. UI 线程在跑一个 event loop，不是在跑 1 kHz 控制。
2. signal 是“通知”，slot 是“反应”；跨线程通常要排队。
3. 控件是 `QObject`，必须待在创建它的线程。
4. 本仓核心库禁止 `#include <QObject>`。

## 3. 先建立直觉：event loop 不是 epoll，也不是周期调度器

你已经在本仓见过两种“等待然后做事”：

```text
CanIoLoop          一个 I/O 线程阻塞在 epoll_wait
                   等 CAN fd / eventfd / signalfd

PeriodicScheduler  一个周期线程按 CLOCK_MONOTONIC 绝对时间醒来
                   做 watchdog / 状态监督
```

Qt UI 是第三种：

```text
QApplication::exec()
  → 取出下一个事件（鼠标、绘制、QTimer、queued signal）
  → 调用对应 slot
  → 再取下一个
```

相同点：都是“循环等待，来了就处理”。  
不同点：

| | Runtime epoll / scheduler | Qt event loop |
|---|---|---|
| 谁拥有 | `RuntimeDaemon` | `QApplication`（UI 线程） |
| 时钟 | `CLOCK_MONOTONIC` | 墙钟/`QTimer`，只服务显示 |
| 能不能卡住 | 卡住会耽误监督 | 卡住会窗口假死、点不了 Cancel |
| 失败语义 | fault / Hold / 停输出 | 界面不刷新；不自动变成 Runtime fault |

所以本仓规定：

- **快读**（`Adapter::snapshot()`）可以留在 UI 线程，用 100 ms `QTimer`；
- **会等的事**（观察窗口 `sleep`、写 JSON 的 `fsync`）必须离开 UI 线程。

不要把 `QTimer(100 ms)` 说成“UI 在跑 10 Hz 控制”。它只是显示刷新。Runtime 自己的
周期线程（当前常见 10 ms）继续独立跑。

## 4. 为什么选 Widgets，不选 QML

**Widgets**（控件）：按钮、表格、标签，用 C++ 直接拼。工业调试台主要是状态和表，不需要
动画页面。

**QML**：用声明式语言画界面，后面再绑 C++。适合触控/动画产品，会多一条语言、一套构建和
一套调试。

本仓第一版只有四个页：Overview / Tests / Diagnostics / Results。用 Widgets 可以直接读
`main_window.cpp`，不必先学 QML 引擎。以后真有复杂图形再评审，不提前引入 Qt Charts。

## 5. signal / slot：把它当成“具名回调”，不要当成线程安全总线

普通 C++ 里，按钮要回调就存一个 `std::function`。Qt 把这件事做成：

```text
发送者.signal(参数)
        │
        ▼
连接表里登记的 slot(参数)
```

本仓里最值得顺着点的四条线：

```text
QPushButton::clicked
  → WorkbenchController::startHealth          （UI 线程）
      → healthRequested(run_id)               （queued，跨到 worker）
          → HealthTestWorker::runHealth       （worker 线程，同步跑测试）
              → completed(TestResult, paths)  （queued，回到 UI）
                  → MainWindow::showHealthResult
```

以及快照：

```text
QTimer::timeout（每 100 ms，UI 线程）
  → WorkbenchController::publishSnapshot
      → snapshotReady(snapshot)
          → MainWindow::updateSnapshot        （只改 Label 文字）
```

### 5.1 `Q_OBJECT` 是什么

写在类里的宏。没有它，`moc`（Meta-Object Compiler，元对象编译器）不会为这个类生成
signal/slot 代码，`connect` 会对自定义 signal 失效。本仓 `HealthTestWorker`、
`WorkbenchController`、`MainWindow` 都有 `Q_OBJECT`。

这是 Qt 特有的编译步骤，不是 C++ 标准。CMake 里 `AUTOMOC` 会在 ON 构建时自动跑。
Qt OFF 时整个 `tools/qt_device_workbench/` 都不会编进 core。

### 5.2 跨线程为什么要 queued

`Qt::QueuedConnection`：先把参数**拷一份**，投进接收者线程的 event loop，接收者空闲时
再调 slot。

因此：

- 拷的类型必须能复制；本仓给 `RuntimeTelemetrySnapshot` 和 `TestResult` 做了
  `Q_DECLARE_METATYPE`（见 `qt_metatypes.hpp`）；
- worker **不能**拿着 `QLabel*` 去 `setText`，那是 UI 线程的对象；
- UI **不能**假设 `runHealth` 已经返回——它在另一个线程。

### 5.3 Cancel 为什么偏偏不用 queued slot

`runHealth()` 里调用的是同步 `CanCommunicationHealthTest::run()`。这段时间 worker
自己的 event loop **没有在转**。如果 Cancel 也 queued 过去，要等测试自己结束才会被看到。

所以 Controller 直接调 `HealthTestWorker::requestCancel()` → 已证明线程安全的
`TestRunner::request_cancel()`。测试循环每个采样间隔自己查取消标志。

这是本仓特意写进注释的例外，**不是**“可以随便跨线程调任何 QObject 方法”。

## 6. 对象树、所有权和关闭顺序

Qt Widgets 常用“父对象负责删子对象”：`new QLabel(page)` 的 `page` 是 parent，窗口拆掉时
一起释放。这和本仓 Runtime 里 `OwnedFd` 的“一个 fd 一个 owner”是同一类想法，只是机制不同。

本仓 Qt 层的 owner 不要和 Runtime owner 混：

| 对象 | 谁拥有 | 谁不能拥有 |
|---|---|---|
| `QApplication` | `main` 栈上 | Runtime |
| `RuntimeDaemon` | `main` 栈上 | `MainWindow` |
| `RuntimeApplicationAdapter` | `main` 的局部作用域 | 不 start/stop daemon |
| `WorkbenchController` / `QTimer` / `QThread` | `main` 局部 / Controller | Window |
| `HealthTestWorker` | 被 `moveToThread`，线程结束 `deleteLater` | Window |
| Label / Button / Table | `MainWindow` | worker |

关闭顺序在 `WorkbenchController` 析构和 `main.cpp` 里已经写死：

```text
停 QTimer
  → requestCancel
  → QThread::quit() + wait()
  → 离开 adapter/controller/window 作用域
  → RuntimeDaemon::stop()
```

先停 UI，再停会读 Adapter 的线程，最后停 daemon。反了就会在已析构的引用上取 snapshot。

`main.cpp` 把 adapter/controller/window 放进内层 `{}`，就是为了保证 `daemon.stop()` 发生在
它们销毁之后。

## 7. 对照源码：四个文件各干什么

读代码时只问所有权和线程，不要记控件 API。源文件顶部和“为什么不能放别处”的注释
已经写在代码里；本文只给地图，不重复翻译每一行 `setText`。

### 7.1 `app/main.cpp`：组装，不实现业务

1. 创建 `QApplication` 并解析 `--can` / `--node-id` / `--results` / `--run-health-once`；
2. 启动并 `boot` `RuntimeDaemon`（失败直接非零退出，此时还没有窗口）；
3. 创建 Adapter（显式 `EvidenceClass::Vcan`，禁止从网卡名猜实物）；
4. 创建 Controller 和 `MainWindow`，`show()`；
5. 若 `--run-health-once`：测完 `quit`，给无显示器的 CI/Gate 用；
6. `app.exec()` 进入 UI 循环；
7. 离开内层作用域后 `daemon.stop()`。

`QString` 和 `std::string` 的转换只发生在这个适配层。headless 测试继续用标准库字符串。

### 7.2 `controller/workbench_controller.*`：用例，不画控件

- 100 ms timer → `adapter.snapshot()` → `snapshotReady`；
- `startHealth` 生成 `qt-vcan-health-<墙钟毫秒>` 这种 run id（墙钟只做文件名，不参与判定）；
- worker 线程里跑**同一个** `CanCommunicationHealthTest` 和 `ResultWriter`；
- 不调用 `daemon.start()` / `stop()`。

### 7.3 `ui/main_window.*`：展示，不判定

四个页只是 Label 和 Table。`updateSnapshot` 把 enum 打成字；`showHealthResult` 把
criteria/diagnostics 填进表，把 JSON/CSV 路径显示出来。

这里没有 `if (heartbeat_age > threshold) FAIL`。阈值在 headless Health Test 里。

### 7.4 `controller/qt_metatypes.hpp`：最小 Qt 污染面

只为 queued 传值注册两种已经存在的 DTO。它包含 workbench 头，**不**包含
`runtime_daemon.hpp`。core 库仍然没有 Qt 类型。

## 8. 和你已经会的 Linux 概念对照

| 你已经知道的 | Qt 里像什么 | 别等同成什么 |
|---|---|---|
| `epoll_wait` 等 fd | `QApplication::exec()` 等 UI 事件 | 不是同一条 I/O 路径，Qt 没打开 CAN |
| `std::function` 回调 | signal/slot | slot 还受线程亲和约束 |
| `std::thread` + 队列 | `QThread` + queued connection | 不要每个按钮一个线程 |
| `OwnedFd` 唯一 owner | parent-child / composition root | Window 仍不能拥有 daemon |
| `CLOCK_MONOTONIC` deadline | `QTimer` 刷新 | Timer 不准、也不该当 watchdog |
| `ResultWriter` 原子写文件 | Results 页显示路径 | UI 不负责 rename/fsync |

## 9. 现在不必学的东西

为了读懂本仓，下面这些可以先放下：

- QML / Qt Quick / Qt Designer；
- `QAbstractItemModel` 整套 Model/View（当前直接填 `QTableWidget`）；
- Qt Charts、OpenGL、网络模块、SQL；
- `QProcess` 去拉起 `rcrd`（当前同进程组合）；
- 自己写 moc 规则（CMake `AUTOMOC` 已处理）；
- 把 DTO 改成 `QVariantMap` 或 `QObject` 属性系统。

等你要加 CAN Monitor 原始帧表或曲线时，再学 Model/View 或自绘，不要为了“更 Qt”先重构。

## 10. 低风险观察（扰动的是显示，不是控制证据）

### 10.1 先证明 core 不需要 Qt

```bash
cmake -S linux -B build/qt-off \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=OFF \
  -DRCR_BUILD_TESTS=ON
cmake --build build/qt-off -j2
ctest --test-dir build/qt-off -R 'test_workbench_' --output-on-failure
```

这一步失败，问题在 headless 合同，不要先怪 Qt。

### 10.2 有 Qt6 时看 target 是否出现

```bash
cmake -S linux -B build/qt-on \
  -DRCR_BUILD_QT_DEVICE_WORKBENCH=ON \
  -DRCR_BUILD_TESTS=ON
cmake --build build/qt-on -j2 --target rcr_qt_device_workbench
```

缺 `qt6-base-dev` 时记录 `not_run`，不要改成 Qt5 凑合编。本仓不做 Qt5 fallback。

### 10.3 无窗口走同一条信号链

需要已有 `vcan0` 和能打开 `PF_CAN` 的权限：

```bash
# 终端 1
build/qt-on/rcr_node_sim --can vcan0 --node-id 1 --heartbeat-ms 20

# 终端 2
QT_QPA_PLATFORM=offscreen \
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --results /tmp/rcr-qt-learn \
  --run-health-once
```

成功时目录里应有 `qt-vcan-health-*.json`，字段仍是 `rcr.workbench.result.v1`，
`evidence` 为 `VCAN`。这证明 **signal → worker → 同一套 Health/Writer**，不证明你看见了窗口，
更不证明物理 CAN。

`QT_QPA_PLATFORM=offscreen` 的意思是：不连显示器，仍创建 `QApplication` 并走 event loop。
它会占用 CPU、做文件 I/O，**不能**当成周期延迟测量。

### 10.4 有显示器时只做理解，不当正式 Gate

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
build/qt-on/rcr_node_sim --can vcan0 --node-id 1 --heartbeat-ms 20
build/qt-on/tools/qt_device_workbench/rcr_qt_device_workbench \
  --can vcan0 --node-id 1 --results workbench-results
```

自己点 Run，对照 Overview 的 heartbeat 和 Results 里的路径。这是学习观察；正式 Phase 4
证据以 offscreen clean Gate 为准。

## 11. 面试可讲与不能讲

可以讲：

- 用可选 Qt6 Widgets 做设备调试台，core Runtime 无 Qt 依赖；
- UI 线程只做展示和发请求；慢测试在 `QThread` worker；快照用 `QTimer` 拉已经线程安全的
  application DTO；
- Cancel 必须走已证明的 `TestRunner` 跨线程握手，因为同步 `run()` 会占住 worker loop；
- 当前同进程，crash containment 还是目标不是现状。

不能讲：

- “我系统学过 Qt / 做过 Qt 架构师级 UI”；
- 已经有 IPC，Qt 崩了设备一定安全；
- offscreen PASS 等于人工视觉验收或实物 CAN；
- `QTimer` 提供硬实时；
- MainWindow 里实现了状态机或测试判定。

## 12. 读完后用五句话自测

1. Qt 在本仓解决什么问题？
2. 点 Run 之后，哪个对象判定 PASS/FAIL？
3. 为什么 snapshot 用 timer、Health 用线程？
4. 为什么 Cancel 不能 queued 到 worker？
5. 现在有什么证据，还缺什么证据？

答不上来就回到对应源文件，不要去翻 Qt 类索引。
