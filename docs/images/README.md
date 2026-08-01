# 系统理解图示

本目录存放帮助建立系统直觉的示意图。它们是教学用图，不是运行时证据，也不能替代
代码、测试或 `evidence/` 下的验收结果。

| 文件 | 看什么 |
|---|---|
| [arch-v1-layers.png](arch-v1-layers.png) | V1 分层：验收 → Runtime Core → SocketCAN → vcan0 → 节点模拟器 |
| [process-vcan-isolation.png](process-vcan-isolation.png) | 两进程只经 `vcan0`，不共享内存 |
| [can-v1-message-flow.png](can-v1-message-flow.png) | Heartbeat / Command / OutputStatus 双向流程 |
| [runtime-fail-closed.png](runtime-fail-closed.png) | worker 退出后命令路径 fail-closed |
| [evidence-boundaries.png](evidence-boundaries.png) | 已建成 / 能证明 / 不能声称 |
| [epoll-timerfd-shutdown-order.png](epoll-timerfd-shutdown-order.png) | `rcr_node_sim`：先从 epoll 摘掉，再关闭业务 fd |

对应代码入口：

- 分层与 Core：`docs/LINUX_RUNTIME.md`、`linux/include/rcr/runtime.hpp`
- 进程验收：`linux/apps/rcr_vcan_acceptance.cpp`、`linux/apps/rcr_node_sim.cpp`
- 关闭顺序：`rcr_node_sim` 退出路径中的 `reactor.remove(...)` 再 `close`
