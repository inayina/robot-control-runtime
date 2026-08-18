# HEAD Reality Audit evidence

class: `LOCAL / VCAN / CURRENT-HEAD / DIRTY`

本目录保存 current-HEAD 进程生命周期、epoll/eventfd/signalfd 关闭顺序，以及普通 Linux
CPU pressure 的软件观察。它不是 Gate closeout，也不是部署验收。

固定限制：

- isolated netns: `permission_denied`（无 `CAP_NET_ADMIN`；`unshare --net` / 用户命名空间均失败；
  `kernel.apparmor_restrict_unprivileged_userns=1`；sudo 需要密码）。**未**绕过，**未**用 docker
  建 vcan。
- host `vcan0` 仅在用户授权停止旧 `rcrd.service` 之后使用；本批实验结束后服务保持 inactive，
  未重启，未 `ip link down`。
- strace 扰动时序，只解释 syscall/关闭顺序，不能当 latency evidence。
- 不是 physical CAN、Orange Pi、hard realtime 或 functional safety。

## Batches

| stamp | HEAD | result |
|---|---|---|
| [20260818T033609Z](20260818T033609Z/NOTES.md) | `3c3bba419491cd6d833b9c55c42eab8aca9757d9` dirty docs | A pass; B1 pass; B2 pass (attach `permission_denied`, child-strace workaround); C ordinary-Linux sample |

## Item status (20260818T033609Z)

| item | status |
|---|---|
| isolated netns | `permission_denied` |
| host vcan0 after authorized old-service stop | used |
| Qt-OFF Debug `/tmp` build | `pass` (pre-existing) |
| A `test_runtime_daemon` until-fail:20 | `pass` (1 skip/round expected) |
| A `DaemonVcanInterfaceDownPropagatesIoError` | `skipped` (no `RCR_ALLOW_IFACE_DOWN`) |
| A `test_rcrd_process` until-fail:10 | `pass` |
| B1 duration/eventfd strace | `pass` |
| B2 live `/proc` threads/fds/scheduler | `pass` |
| B2 `strace -p` attach | `permission_denied` (`yama.ptrace_scope=1`) |
| B2 SIGTERM via strace-as-parent | `pass` |
| C `rcr_benchmark --help` non-zero | observed CLI; not a benchmark fail |
| C 5 baseline + 5 stress SCHED_OTHER | `pass` (sample only) |
| physical CAN / Orange Pi / PREEMPT_RT / safety | `not_run` |

A prior interrupted lifecycle repeat (Ctrl-C 130) is not formal evidence and was not merged.
