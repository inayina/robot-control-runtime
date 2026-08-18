# Runtime Process / Thread Model

状态：Current explanatory model  
适用提交：以 [HEAD Reality Audit](HEAD_REALITY_AUDIT.md) 记录的 SHA 为快照；进程合同变化后必须复核  
Authority 边界：本文件解释进程、线程、阻塞与退出，不改变 `ARCHITECTURE.md` 的系统关系、
`CODE_OWNERSHIP_MAP.md` 的代码 owner 或 `RCRD_CONTRACT.md` 的 standalone daemon 合同。

## 1. 为什么需要跨宿主模型

`RuntimeDaemon` 只有一个实现，但当前有 standalone `rcrd` 和主演示 `rcr_cell_app` 两个替代
宿主。只看 `RCRD_CONTRACT.md` 会漏掉 CEL1、CellReady 与 Modbus agent；只看 Qt 会误以为 UI
拥有 Runtime。本文把操作系统真正调度的进程/线程列出来。

不选“每个 executable 一张独立图”：公共 Runtime 线程会被重复描述并漂移。这里先定义公共
核心，再列各宿主增加的职责。

## 2. 演示部署进程图

```text
ThinkPad
┌──────────────────────────────────────────────┐
│ rcr_qt_device_workbench --cell-peer          │
│  UI thread + health worker + modbus worker   │
└───────────────────┬──────────────────────────┘
                    │ CEL1/TCP
                    ▼
Orange Pi
┌──────────────────────────────────────────────┐
│ rcr_cell_app                                 │
│  main: CEL1 poll + CellReady edge action     │
│  scheduler: Runtime tick                     │
│  I/O: SocketCAN epoll                        │
└───────────────┬───────────────────────┬──────┘
                │ CAN                   │ localhost TCP
                ▼                       ▼
          STM32F103             rcr_modbus_rtu_agent
                                single thread → /dev/ttyS7
```

主演示启动前停止 standalone `rcrd`，保证 `can0` 只有一个应用 owner。关闭 Qt 不应停止 Orange Pi
上的 Runtime 或 CellReadyMapper。

## 3. 公共 Runtime 核心线程

### 3.1 main/application

输入：CLI/config、外部应用调用、停止意图。  
输出：组件 start/stop、退出码、聚合 snapshot。  
阻塞：standalone `wait_and_stop` 每 20 ms 观察 worker；cell app main 使用 CEL1 `poll(20 ms)`。

main 不执行周期 watchdog，也不读 SocketCAN。它拥有进程装配与销毁顺序。

### 3.2 PeriodicScheduler worker

输入：绝对单调时间边界、Runtime state、bounded input queue。  
输出：tick stats、watchdog/ACK timeout、NodeSupervisor 状态变化。  
阻塞：`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`。  
资源：拥有自己的 `std::thread` 和每线程 affinity/scheduling 属性，不拥有 fd。

失败时记录 `worker_error` 并退出 worker；main 必须同时观察 scheduler 与 I/O，避免 scheduler
已死而 epoll 仍健康时进程永久等待。

### 3.3 CanIoLoop worker

输入：SocketCAN readiness、eventfd stop、signalfd shutdown、Runtime mailbox。  
输出：decoded input queue、CAN output、I/O stats/stop reason。  
阻塞：`epoll_wait`，当前最多 10 ms；SocketCAN 本身 nonblocking。  
资源：独占 CAN socket、epoll 和 I/O thread；只借用 daemon 的 eventfd/signalfd。

单次 CAN wake 有 frame budget。stop/signal 优先于 CAN，避免总线洪泛延迟退出。

### 3.4 optional duration helper

只有 `--duration-ms > 0` 才创建。它用 `steady_clock` 与 20 ms `sleep_for` 到期后调用
`request_stop()`；不是 Runtime 周期线程，也不参与控制语义。

## 4. 各进程增加的线程与阻塞

| 进程/模式 | 代码显式线程 | 主要阻塞 | 退出 |
|---|---:|---|---|
| `rcrd` 默认 | 3 | main wait、absolute sleep、epoll | signalfd/eventfd → bounded join |
| `rcrd --duration-ms` | 4 | 再加 duration sleep | helper 请求 stop |
| `rcr_cell_app` | 3 | main CEL1 poll；edge Modbus connect/write/read | I/O stop reason 被 main 轮询发现 |
| Modbus agent | 1 | accept poll、client read、serial poll/read/write | 当前无 graceful signal path |
| Qt `--cell-peer` | 3 个显式应用线程 | Qt event loop、worker work | controller quit/wait workers |
| Qt local Runtime | 5 个显式应用线程 | 上述 Qt + Runtime scheduler/I/O | 先 Qt worker，再 daemon teardown |
| node simulator | 1 | 自有 epoll 等 CAN/signalfd/timerfd | signal/duration |

Qt、libc、Go runtime 或图形后端可能创建内部线程；表中数字只表示项目代码显式拥有的线程，
精确 live 数必须读取 `/proc/<pid>/task`。

## 5. TCP、CAN、串口在哪个线程

```text
CEL1 TCP             rcr_cell_app main thread       poll
CellReady decision   rcr_cell_app main thread       snapshot + pure evaluation
agent TCP client     rcr_cell_app main thread       synchronous connect/request
SocketCAN            CanIoLoop worker               epoll + nonblocking read/write
Runtime watchdog     PeriodicScheduler worker       absolute periodic tick
agent TCP server     rcr_modbus_rtu_agent main      poll/accept/read/write
physical serial      rcr_modbus_rtu_agent main      write/tcdrain/poll/read
Qt UI                Qt main thread                 QApplication event loop
Qt health/modbus     two QThread workers            queued signals/slots
```

不能把 Qt event loop、POSIX `poll`、Runtime epoll 和周期 scheduler 说成同一个事件循环。

## 6. Shutdown sequence

### 6.1 standalone `rcrd`

```text
SIGINT/SIGTERM → signalfd → I/O stop
internal/duration stop → eventfd → I/O stop
→ main wait returns
→ join I/O
→ join duration helper
→ final snapshot/log
→ stop/join scheduler
→ destroy I/O/supervisor/runtime/queue
→ close eventfd/signalfd and restore main signal mask
```

### 6.2 `rcr_cell_app`

I/O worker 处理 signal 后发布 stop reason；main 最多在下一轮检查时退出 loop，再 disconnect Modbus
client、close CEL1 clients/listener、请求 daemon stop 并执行公共关闭。同步 Modbus transaction 的
timeout 是 shutdown latency 的潜在上界之一，需要实验而不是从代码宣称实际延迟。

### 6.3 Modbus agent

当前无限 `serve_one` 循环没有 stop token。正常进程终止会由内核关闭 TCP/serial fd，但没有机会
记录最终状态、结束当前 RTU transaction 或给 systemd 返回分类退出码。这是明确 gap；是否补齐
取决于 current Gate 的真实部署需要，不因文档审计自动编码。

## 7. Restart generation

一次新 Runtime 进程是一个新 generation：

- mode、fault、mailbox、ACK、queue、trace 和 node understanding 全部重建；
- 必须重新观察 heartbeat/session，并由工程站显式 Activate；
- 旧 command/session/deadline 不持久化、不自动重放；
- MCU 若继续运行，其 lease 与实际输出收敛属于设备 owner；Linux 不能从旧本地 state 假定设备
  仍处于某一输出状态；
- Platform 的管理 session 不是 CAN session，也不是 Runtime process generation。

## 8. 验证方法与干扰

低风险观察：

```bash
ps -L -o pid,tid,cls,rtprio,pri,psr,stat,wchan:24,comm -p <PID>
find /proc/<PID>/task -mindepth 1 -maxdepth 1 -type d -printf '%f\n'
```

syscall 观察：

```bash
strace -ff -tt -e trace=epoll_wait,read,write,clock_nanosleep,close \
  ./build/linux/rcrd --can vcan0 --duration-ms 500
```

`strace` 会明显扰动时序，只能解释调用/关闭关系；调度 latency 必须用无 strace 的 benchmark。

## 9. 面试检查

1. 进程与线程分别共享什么？fd table 属于谁？
2. 为什么 `SCHED_FIFO`/affinity 要在 worker 内申请？
3. scheduler worker 异常而 epoll 仍健康时 main 为什么必须主动观察？
4. 为什么 CellReady 不放进周期线程或 CAN decoder？
5. 为什么进程重启不能恢复旧 mailbox？

