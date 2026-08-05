# `rcrd` 进程合同（P1 冻结）

状态：Frozen（P1-G0）  
关联：[P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md)

本文冻结可部署 Runtime daemon 的参数、退出码、线程图和故障映射。实现必须以本文为准；
变更合同等于新的版本讨论，不能在编码中静默改语义。

## 1. 最小 CLI 参数

| 参数 | 默认 | 含义 |
|---|---|---|
| `--can IFACE` | `vcan0` | 已存在的 CAN 接口名；daemon **不**创建接口 |
| `--node-id N` | `1` | 监督的唯一节点，范围 1..31 |
| `--period-ms N` | `10` | 周期监督线程周期 |
| `--command-timeout-ms N` | `100` | 普通输出命令 watchdog |
| `--output-ack-timeout-ms N` | `100` | 已发送命令等待匹配 `APPLIED` 的门限；超时进入 Hold |
| `--heartbeat-timeout-ms N` | `300` | 距上次合法 Heartbeat 的 CommLoss 门限（与 CAN V1 合同一致） |
| `--fifo-priority N` | `0` | `0`=不请求 FIFO；`1..99` 请求 `SCHED_FIFO` |
| `--require-fifo` | off | 设置失败则启动失败（退出码 Permission） |
| `--cpu-affinity N` | 未设置 | 绑定周期与 I/O 线程到该 CPU；未配置则不绑定 |
| `--duration-ms N` | `0` | `0`=直到 SIGINT/SIGTERM；否则运行后主动停止 |
| `--help` | — | 打印用法后退出 0 |

P1 **不**装载 `linux/configs/runtime_v1.yaml`。该文件仍为草案；systemd `ExecStart`
足以承载 V1 少量参数。不引入 YAML 库。

独立运行的 `rcrd` 只做生命周期与节点监督，**不**自动发送演示输出。输出发送由测试经
Application 服务 API（同进程）驱动，避免生产 daemon 留下测试命令入口。

## 2. 退出码

| 码 | 符号 | 触发 |
|---|---|---|
| 0 | `Ok` | 正常 SIGTERM/SIGINT，或 `--duration-ms` 到期后有界退出 |
| 1 | `ConfigError` | 参数非法、`--help` 以外的用法错误 |
| 2 | `InterfaceError` | CAN 接口缺失/非 CAN、socket/epoll/eventfd/signalfd 创建失败 |
| 3 | `PermissionError` | `--require-fifo` 下 `SCHED_FIFO` 失败，或 affinity 强制失败 |
| 4 | `WorkerFailure` | 周期线程异常、I/O 不可恢复错误、发送失败后的 fail-closed |

配置/接口错误不得留下 scheduler 或 I/O 线程。

## 3. 线程与资源 owner

```text
main
  ├─ 解析参数、block SIGINT/SIGTERM、构造 RuntimeDaemon
  ├─ start → wait（信号或 duration）→ stop → 退出码
  │
  ├─ PeriodicScheduler（Runtime 拥有）
  │     └─ watchdog + NodeSupervisor 消费有界队列（每周期有预算）
  │
  └─ I/O thread（Daemon 拥有）
        └─ epoll(SocketCAN, eventfd, signalfd)
```

| 资源 | Owner | 关闭原则 |
|---|---|---|
| `SocketCan` | I/O loop | 先 epoll DEL，再 RAII close |
| epoll fd | I/O loop | I/O 线程结束后关闭 |
| eventfd | Daemon lifecycle | 写停止 → join I/O → 关闭 |
| signalfd | Daemon lifecycle | main 建线程前 block；join 后关闭 |
| 有界输入队列 | Daemon composition | I/O 生产、周期消费；满则锁存 Fault |
| scheduler | `LinuxRuntime` | request_stop、join 后销毁 |

## 4. 故障 → 状态 / 退出映射

| 条件 | snapshot/trace | 退出 |
|---|---|---|
| 节点首次合法 Heartbeat | 记录 online、boot/session | 继续运行 |
| Heartbeat 超时（默认 300 ms） | `FaultCode::CommLoss`，`FaultDetected` | 继续；外部可 stop |
| 已发送命令 ACK 超时 | `ack_timeout_count` 增加，Active → Hold，清输出；不自动重试 | 继续；需显式恢复 |
| stale/session mismatch/乱序 OutputStatus | 更新 `last_ack_*` 与 `unexpected_ack_count`；不得冒充确认 | 等待正确 ACK 或超时 Hold |
| 节点 boot/session 变化 | 显式重启事件；清旧会话理解；离开 Active | 继续；需显式恢复 |
| 协议/解码拒绝 | 计数 + 可选 `ProtocolReject` 故障锁存 | 单帧不退出 |
| 有界队列溢出 | 计数 + `FaultCode::Internal` 锁存，禁止保持 Active | 继续 |
| CAN `EPOLLERR/HUP` 或不可恢复 read/write | worker 错误可见 | `WorkerFailure` |
| 周期 callback 异常 | scheduler `worker_error`；输出路径关闭 | `WorkerFailure` |
| SIGTERM / 内部 stop | 清空输出路径，有界 join | `Ok` |

恢复必须显式：FaultCleared / Resume / 新 Activate；不得自动重放旧命令。

## 5. 测试边界

- 服务级验收：测试进程构造 `RuntimeDaemon`，另启独立 `rcr_node_sim`；只经 `vcan0`。
- 进程级验收：fork/exec 独立 `rcrd`，验证信号、退出码与 fd 回收。
- 不为测试建立 REST、Unix socket 或复杂 `rcrctl`。
