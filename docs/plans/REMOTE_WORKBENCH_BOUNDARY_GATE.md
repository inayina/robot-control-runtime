# Remote Workbench Boundary Gate

状态：**Closed（local/dirty verification；下一 Gate 未选择）**  
授权日期：2026-08-13（用户从 `SYSTEM_CONVERGENCE_AUDIT` 候选 **B** 明确选择）  
关闭日期：2026-08-13  
证据等级：`LOOPBACK / NO PHYSICAL PC-ARM`  
长期参考：[PC_ARM_DEVICE_CONVERGENCE_PLAN.md](PC_ARM_DEVICE_CONVERGENCE_PLAN.md) §3

## 1. 为什么现在做

当前 Qt Workbench 与 `RuntimeDaemon` **同进程**。它能验证 commissioning 用例，但不能证明
PC operator 与 Orange Pi Runtime 的 failure-domain 分离，也不能练习 framing、重连和跨主机
应用合同——这些是 PC→ARM→Device 收敛里缺的应用边界。

本 Gate 不把现场总线搬进网络，也不重写正式 `rcrd`。它只建立薄的 Runtime 应用边界：

> PC Qt / headless client ↔ TCP 控制面（+ 可选 UDP telemetry）↔ ARM 侧 endpoint ↔ 现有
> `RuntimeApplicationAdapter` DTO

证据停在 **localhost loopback**。物理 ThinkPad↔Orange Pi Ethernet 必须另开 Gate。

## 2. 本 Gate 的唯一实现链

```text
WorkbenchController（保留 Local 路径）
        │
 Runtime Client Contract（薄；非 plugin framework）
   ┌────┴────┐
 Local path   RemoteRuntimeClient
 (现有 adapter)        │ in-process bytes（M1/M2；非 QTcpSocket）
                       ▼
              RemoteControlEndpoint
                       │
              RuntimeApplicationAdapter DTO 投影
                       │
                 RuntimeDaemon（同进程 fixture；未改正式 rcrd）
```

- `MainWindow` 增加 Connection 状态展示；不拥有 socket，不做 framing/CRC/重连判定。
- Controller 编排 Local vs Remote；M2 无真实 socket，会话在 UI 线程完成。
- 纯 C++ frame codec / parser / endpoint / client 在 `rcr_workbench`，不依赖 Qt。
- Endpoint 只暴露应用 DTO 字段；禁止网络 `memcpy` Runtime 私有结构。
- TCP 语义消息：`HELLO` / `HEARTBEAT` / `GET_STATUS`。**未**开放 `COMMAND`。
- UDP：**未实现**。不引入 gRPC、ZMQ、HTTP/WebSocket、UDS。

## 3. M0–M3 退出条件

### M0：authority 与边界 — **pass（2026-08-13）**

### M1：headless framing + loopback 控制面 — **pass（local/dirty，2026-08-13）**

### M2：Qt Connection 薄层 — **pass（local/dirty，2026-08-13；本轮不做 UDP）**

### M3：验证与收口 — **pass（local/dirty，2026-08-13）**

- fresh Qt OFF：**28/28**；fresh Qt ON：**29/29**（offscreen QtTest）；宿主机 `vcan0`。
- Workbench README、GATES、知识库 §6.16、模块卡 44–45 与代码一致。
- 明确未实现：物理 PC–ARM、UDP、`COMMAND`、正式 `rcrd` remote、产品级 crash isolation。
- 摘要：
  [`evidence/portfolio/remote_workbench_boundary_gate_20260813.md`](../../evidence/portfolio/remote_workbench_boundary_gate_20260813.md)。
  `git_dirty=true`；不是 clean release evidence。

## 4. 关闭记录（2026-08-13）

| Milestone | 结果 | 证据边界 |
|---|---|---|
| M0 authority | pass | `LOOPBACK / NO PHYSICAL PC-ARM`；无 Serial/EtherCAT/A2 |
| M1 framing + loopback | pass | 半包/粘包/CRC；HELLO/HB/GET_STATUS；Qt OFF |
| M2 Qt Connection | pass | Connection 页 signal/slot；无 socket；Local 路径保留；无 UDP |
| M3 fresh verification | pass | Qt OFF 28/28；Qt ON 29/29；vcan 宿主复跑 |

关闭后不自动启动物理 PC–ARM、UDP、COMMAND、V1 clean、physical CAN、RS-485 或 EtherCAT。
下一 Gate 只从
[`SYSTEM_CONVERGENCE_AUDIT.md`](../SYSTEM_CONVERGENCE_AUDIT.md) 的 `NEXT_GATE_REVIEW` 重新选择。

## 5. 不在本 Gate 的范围

| 项 | 原因 |
|---|---|
| 物理 ThinkPad ↔ Orange Pi | 另开 `PHYSICAL_PC_ARM_REMOTE_GATE` |
| `COMMAND` / DO / 运动 lease | 需单独恢复与 session 合同 |
| 改写正式 `rcrd` 为唯一入口 | 先用 loopback fixture / 薄 endpoint |
| gRPC / ZMQ / UDS / shm | 计划明确延期 |
| UDP telemetry | 本轮显式跳过；不得声称已关 |
| Serial/Modbus RTU、EtherCAT、A2 | 其他候选 |

## 6. Stop rules（关闭后仍有效）

- 在 `MainWindow` 内直接操作 socket、解析半包或判定 reconnect；
- 序列化 Runtime 私有结构、fd、pointer 或内部队列到网络；
- 为演示开放未定义 lease 的运动/输出命令；
- 建立 `ITransport` / device plugin / 多协议超级框架；
- 把 loopback 写成 physical PC–ARM 或 crash isolation 已验收；
- 修改正式 `rcrd` 退出码/线程合同来“顺便”塞 remote；
- 把 Modbus TCP 实验或 Dashboard HTTP 冒充本 Gate 证据。
