# FD / Event Model

状态：Current explanatory model  
适用范围：Linux Runtime、主演示宿主、Modbus agent 和 node simulator 的 fd ownership  
不承担：协议 authority、Runtime recovery policy、物理设备验收或通用 transport 设计

## 1. 最小心智模型

fd 是进程 fd table 中指向内核对象的小整数，不是 owner。`epoll_ctl(ADD)` 只让 epoll 实例观察
该 fd，不转移 close 责任。业务 owner 必须先停止并 join 唯一访问线程，再从 epoll 删除业务 fd，
最后 close；旧整数随后可能被内核复用。

本仓选择一个明确 owner + movable RAII，不用 shared ownership。备选的裸 `int` 需要每条早退路径
手工 close，容易 double close 或 leak；通用 reactor/transport 则没有第二个真实 backend 支持。

## 2. `rcrd` steady-state inventory

| 内核对象 | 创建者 / owner | blocking flag | 谁等待 | epoll interest | 关闭 |
|---|---|---|---|---|---|
| PF_CAN RAW socket | `SocketCan` / `CanIoLoop` | `O_NONBLOCK` | I/O worker | LT `IN/ERR/HUP` | DEL → `SocketCan::close` |
| epoll instance | `EpollReactor` / `CanIoLoop` | n/a | I/O worker | interest list owner | worker join 后析构 |
| eventfd | `EventFd` / `RuntimeDaemon` | NONBLOCK+CLOEXEC | I/O worker | LT `IN` | request/drain → join → close |
| signalfd | `SignalFd` / `RuntimeDaemon` | NONBLOCK+CLOEXEC | I/O worker | LT `IN` | drain → join → close/restore mask |
| stdin/stdout/stderr | parent/systemd | target dependent | app/logging | 无 | process exit |

短寿命启动探测 fd 与 C++ stream 在作用域退出时关闭，不计入 steady-state。Runtime 没有 timerfd、
pipe、Unix socket、shared memory 或持久 trace fd。

## 3. epoll ownership

```text
CanIoLoop owns
  ├─ SocketCan fd
  ├─ EpollReactor fd
  └─ I/O thread

RuntimeDaemon owns
  ├─ EventFd ───── borrowed native_handle ─┐
  └─ SignalFd ─── borrowed native_handle ─┤
                                           ▼
                                   EpollReactor interest list
```

`native_handle()` 是 non-owning observation；`CanIoLoop` 不得 close daemon 的 eventfd/signalfd。
teardown 先 DEL 三个业务 fd，再关闭 CAN；daemon 只有在 I/O join 后才销毁 stop/signal fd。

## 4. Readiness 不是业务完成

level-triggered epoll 表示“此刻继续操作大概率不会阻塞”。它不保证：

- 一次 read 就能读完所有帧；
- 返回后 fd 不会被另一事件改变；
- `EPOLLIN` 不会同时伴随 `EPOLLERR/HUP`；
- write 一定成功；nonblocking send 仍可能 `EAGAIN`。

CAN RX 循环同时满足两个停止条件：读到 `EAGAIN` 或用完 `max_frames_per_wake`。budget 防止 CAN
洪泛独占线程；level-triggered 会让仍有数据的 fd 在下一次 wait 再次 ready。若未来改 EPOLLET，
就必须建立“每次持续 drain 到 EAGAIN”的新合同，不能只改 flag。

## 5. 三条 event path

### 5.1 CAN input

```text
driver/netdevice queue
→ epoll IN
→ read can_frame
→ decode by value
→ bounded queue try_push
→ scheduler tick consumes with budget
```

queue 满不能静默覆盖 heartbeat/restart/fault edge；当前会计数并锁存内部 fault。

### 5.2 Output command

```text
application publishes latest-wins mailbox
→ I/O worker wakes for any fd event or 10 ms timeout
→ consumes latest target
→ rechecks mode/session/deadline
→ nonblocking CAN write
→ registers pending ACK
```

10 ms epoll timeout 是无 CAN 流量时推进 output pump 的当前最小机制，不是 control period，也不是
ACK timeout。

### 5.3 Stop and signal

```text
internal stop: write(eventfd) ─┐
                              ├→ epoll IN → drain → set stop flag
SIGINT/SIGTERM: signalfd ──────┘
```

eventfd 解决“另一个线程怎样唤醒正在 epoll_wait 的 I/O worker”；condition variable 不能直接
唤醒该 syscall。signalfd 解决“怎样不在受限 async signal handler 中执行关闭逻辑”。

## 6. 非 Runtime fd

| 进程 | fd | 模式 | 等待机制 | owner |
|---|---|---|---|---|
| `rcr_cell_app` | CEL1 listener + ≤8 clients | nonblocking | main `poll` | `CellAppServer` |
| `rcr_cell_app` | agent TCP client | nonblocking connect；有界 request | main `poll/read/write` | `ModbusAgentClient` |
| Modbus agent | TCP listener/client | listener/client blocking + poll gate | main `poll/accept/read/write` | `ModbusAgentServer` |
| Modbus agent | `/dev/ttyS7` | blocking fd + `poll` receive | main `write/tcdrain/poll/read` | `PosixSerialPort` |
| node simulator | CAN/epoll/signalfd/timerfds | nonblocking event sources | simulator epoll | simulator process |

这些 fd 属于不同进程或宿主。没有理由为了“一个 epoll”把串口和 CEL1 搬进 Runtime Core。

## 7. Error/HUP 与 failure behavior

- CAN `EPOLLERR/HUP`：投递 I/O error，I/O worker 停止，daemon 分类为 worker failure；
- CAN read/write 不可恢复错误：记录 errno，停止 I/O；write `EAGAIN` 只保留 pending 并稍后重试，
  每次重试前重新检查 authority/deadline；
- eventfd 重复 write：计数累积；drain 到 `EAGAIN`；
- signalfd drain：读取完整 `signalfd_siginfo`，不在 handler 里做 C++ 工作；
- CEL1 client HUP/error：丢弃该 client，不转移 Runtime ownership；
- serial timeout：返回 Modbus offline/timeout 语义，不伪装成 CAN CommLoss。

## 8. Close 与 restart

安全顺序：

```text
publish stop intent
→ wake blocking waiter
→ waiter exits event loop
→ join 唯一访问线程
→ epoll DEL business fds
→ close business fds
→ close epoll/eventfd/signalfd
```

进程异常退出时内核会回收 fd，但这只能防止跨进程永久 fd leak，不能证明同进程重复
start/stop 不泄漏，也不能证明协议事务完成。后者必须比较同进程 `/proc/self/fd` 与 thread count。

## 9. 验证

不扰动的基础观察：

```bash
find /proc/<PID>/fd -mindepth 1 -maxdepth 1 -printf '%f %l\n' | sort -n
find /proc/<PID>/task -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -n
```

如果权限拒绝，记录 `permission_denied`，不能改用源码推断并写成 live pass。

syscall 关系观察：

```bash
strace -ff -tt -e trace=epoll_create1,epoll_ctl,epoll_wait,eventfd2,signalfd4,\
socket,read,write,close ./build/linux/rcrd --can vcan0 --duration-ms 500
```

strace 会改变调度和系统调用耗时，不用于 latency evidence。

## 10. 面试检查

1. epoll 是否拥有被注册 fd？
2. 为什么 LT + budget 不要求一次 drain 到 EAGAIN？
3. 为什么 eventfd 不能被 condition variable 直接替代？
4. 为什么只证明子进程退出不能证明同进程无 fd leak？
5. 为什么 timerfd 出现在 simulator，却不在 Runtime steady-state inventory？

