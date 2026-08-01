# 当前阶段审计与开发计划

状态：Active  
审计日期：2026-08-01  
当前目标：在 ThinkPad 上完成 CAN V1 线协议、独立节点模拟器和可重复的 `vcan`
进程间验收，为下一阶段 `rcrd` 的真实 fd 生命周期提供稳定输入。

本文是短周期执行文档。长期阶段顺序仍以
[DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) 为准，系统边界仍以
[SPEC.md](../SPEC.md) 和 [AGENTS.md](../AGENTS.md) 为准。

## 1. 审计结论

当前仓库已经形成可构建的 Linux Core，而不是只有架构文档；但尚未形成 V1
端到端 Runtime。最准确的阶段判断是：**阶段 0 有可运行首版但仍有关闭项，阶段 1
尚未开始，阶段 2 及以后没有实现证据。**

### 1.1 已确认能力

- `linux/` 可由 GCC 13.3 以 C++20 Debug 配置从全新目录完成构建，无第三方运行时依赖；
- 10 个 CTest 可执行目标、36 个测试用例覆盖 scheduler、状态机、mailbox、watchdog、
  trace、Runtime、SocketCAN/FakeCanBus、vcan 辅助和 epoll；
- 普通 Debug 构建的 10 个测试目标全部通过；
- ASan + UBSan 构建在关闭本环境不支持的 LeakSanitizer 后，10 个测试目标全部通过；
- `rcr_benchmark` 能输出普通调度下的周期、唤醒延迟、miss 和 FIFO 降级状态；
- README、SPEC 与实现对“软件联锁不是功能安全”“普通 Linux 不是硬实时”“vcan
  不是物理 CAN 证据”的描述基本一致。

### 1.2 证据限制

- 本机没有 `vcan0`。`test_socketcan_vcan` 的回环用例打印 `skip` 后仍被自建测试框架
  记为 `PASS`，因此本次 10/10 不包含有效的 SocketCAN 回环证据；
- ThreadSanitizer 在当前执行环境启动时因 `unexpected memory mapping` 失败，未执行到
  被测代码，不能据此判断存在或不存在数据竞争；
- LeakSanitizer 在当前受 ptrace 约束的环境无法运行；ASan/UBSan 结果不包含泄漏检查；
- 500 ms、1 ms 周期的单次空载运行得到 500 cycles、0 miss，只是工具冒烟测试。
  它没有环境元数据、压力对照或分位数，不能保存为性能基线；
- 仓库仍处于 `No commits yet on main`，所有文件均未跟踪，尚无可引用的基线 commit；
- `linux/configs/runtime_v1.yaml` 当前没有装载路径，只能视为配置草案。

## 2. 风险与处置优先级

| 优先级 | 发现 | 工程影响 | 计划处置 |
|---|---|---|---|
| ~~P0~~ | ~~`try_setup_vcan()` 通过 `std::system()` 执行 `ip`~~ | ~~shell / 库承担网络配置~~ | **已关闭（G0）**：库内删除创建能力；只读 `probe_can_interface`；运维入口 `setup_vcan.sh` |
| ~~P0~~ | ~~可选 vcan 回环缺失时仍显示 PASS~~ | ~~假阳性~~ | **已关闭（G0）**：`SKIP_RETURN_CODE=77`；`--require-vcan` 阶段验收 |
| ~~P1~~ | ~~`vcan_exists()` 只判断 net 目录~~ | ~~非 CAN 同名误判~~ | **已关闭（G0）**：校验 `/sys/.../type == ARPHRD_CAN`，区分 Missing/NotCan |
| ~~P1~~ | ~~`SocketCan` 无 native handle~~ | ~~epoll 被迫绕过封装~~ | **已关闭（G0）**：具体类上非 owning `native_handle()` |
| ~~P1~~ | ~~scheduler worker 错误只进 stats~~ | ~~控制关闭与状态可见性差~~ | **已关闭（G0）**：冻结 fail-closed 合同并补测试；daemon 升级留阶段 2 |
| ~~P1~~ | ~~CAN V1 只有逻辑消息，没有字段预算、ID、字节序、计数器回绕和 golden vectors~~ | ~~无稳定线级合同~~ | **已关闭（P1）**：见 `protocol/can_v1/README.md` |
| P2 | benchmark 只有 min/mean/max，缺元数据、分位数和负载矩阵 | 只能冒烟，不能形成跨平台可比较证据 | 保留到阶段 3 集中实现，不阻塞 CAN V1 |
| P2 | sanitizer 没有仓库内 CMake preset/option 或固定命令 | 当前结果依赖审计者临时 flags，复现性弱 | CAN 端到端稳定后，在阶段 3 加入正式 sanitizer 配置和 CI/脚本 |

## 3. 下一阶段范围与方案选择

### 3.1 所属层与职责

本阶段跨越两层，但不改变 Runtime Core 边界：

```text
protocol/can_v1/          固定线级合同、字段语义和 golden vectors
        ↓
linux CAN V1 codec        CanFrame 与明确的 wire message 之间编解码
        ↓
rcr_node_sim              独立进程；拥有 SocketCan、定时源和模拟节点状态
        ↓
vcan0                     唯一进程间通信路径
        ↓
阶段验收进程              发送命令、观察 heartbeat/status、判断故障场景
```

codec 不拥有 fd、线程或节点状态；模拟器不链接 `LinuxRuntime`，也不直接访问测试进程
内存。Runtime daemon、signalfd/eventfd、systemd、日志和 Orange Pi 部署不属于本阶段。

### 3.2 关键选择

- 选择经典 CAN 8-byte 帧；当前四类消息没有长报文需求，不引入 ISO-TP；
- 线协议使用独立的固定宽度 wire 类型，不复用 `rcr::OutputCommand` 的 64 位进程内布局；
- 不在线上传输绝对 `CLOCK_MONOTONIC` deadline。Linux 与未来 MCU 没有共同单调时钟，
  `OutputCommand` 应传相对有效期，接收端在本地转换为 deadline；
- CAN ID 承担消息类别和节点寻址，payload 预算给版本、boot/session、sequence、状态、
  输出与有效期；所有窄计数器必须定义回绕比较规则；
- 模拟器选择单线程 fd 循环，拥有 SocketCAN 与 Linux 定时 fd。相比“scheduler 线程 +
  阻塞 receive 线程”，该方案的所有权和退出顺序更容易验证，也能提前验证 epoll 的真实
  fd 用法；
- Fault Injection 只通过默认关闭的模拟器启动参数选择。正式 CAN 消息不提供制造故障的
  命令；
- V1 配置先使用边界明确的命令行参数，不为一个模拟器引入 YAML 解析库，也不手写通用
  YAML parser；现有 YAML 草案留到 daemon 配置方案确定时处理。

## 4. 工作包

### 所有工作包的学习交付物

用户当前只大致了解 C/C++、Linux 内核和操作系统通信，因此每个工作包除代码与测试外，
还必须在 [C/C++、Linux Runtime 与面试知识库](KNOWLEDGE_BASE.md) 中补充对应知识卡：

1. 先用直觉语言解释问题，再说明用户态/内核态边界和本仓调用链；
2. 解释线程、时间、资源所有权、关闭顺序和 fail-closed 行为；
3. 比较至少一个合理备选，说明当前规模下为什么不选；
4. 提供一个不会被误作性能证据的观察实验；
5. 给出项目化面试回答、常见追问，以及“尚未实现/尚未测量”的边界。

知识卡与相关中文设计注释缺失时，即使测试通过也不算工作包完成。源码注释只解释设计
意图和不可见约束，基础教程与长篇面试材料放入知识库。

### G0：关闭 Core 前置风险

状态：**已完成（2026-08-01）**

输入：现有 vcan 辅助、SocketCan、scheduler 和测试。  
输出：安全、可被 epoll 使用且不会产生假阳性的 Linux I/O 基础。

已完成：

1. 删除库内 `try_setup_vcan()` 及 `std::system`；`setup_vcan.sh` 为唯一创建入口并校验 ARPHRD_CAN；
2. `probe_can_interface` / `can_interface_available` 区分 Missing、NotCan、InvalidName；
3. 错误路径与可选回环拆分；CTest `SKIP_RETURN_CODE=77`；`--require-vcan` 阶段验收；
4. `SocketCan::native_handle()` 非 owning，覆盖打开/移动/关闭；
5. scheduler 异常、重复 start/stop、Runtime worker 丢失后 publish/consume 关闭的测试；
6. `docs/LINUX_RUNTIME.md` 冻结 worker 失败合同。

退出条件：普通 Debug 测试通过；vcan 缺失明确显示 Skip；库代码不再调用 shell。

### P1：冻结 CAN V1 合同

状态：**已完成（2026-08-01）**

输入：四类逻辑消息及正常、重启、乱序、超时、非法帧场景。  
输出：`protocol/can_v1/README.md` 的完整线级合同和一组固定 golden vectors。

已完成：

1. 每类消息 8-byte 字段预算与 500 kbit/s 负载粗算；
2. 标准 11-bit ID（function<<5|node）、大端、版本/保留位/非法值行为；
3. boot/session 生成、u16 回绕比较、heartbeat 100 ms / 超时 300 ms；
4. `validity_10ms`（10..2500 ms）到接收端本地 deadline 的换算与上下限；
5. RTR/EFF/DLC/版本/未知节点与保留位非零的拒绝表；
6. 每类最小/典型/边界/非法向量（README §10 与 `golden_vectors.tsv`）。

退出条件：字段均可回答生成方、变化时机、编码、回绕与非法处理；合同不引用 C++ 布局。

### P2：实现无状态 codec

状态：**已完成（2026-08-01）**

输入：冻结合同和 golden vectors。  
输出：显式 `encode`/`decode` API 与单元测试。

已完成：

1. `rcr::can_v1` wire DTO 与内部 `OutputCommand` 分离；
2. 逐字段大端编解码，无 `reinterpret_cast` / 结构体 memcpy；
3. encode 范围校验；decode 校验 ID、RTR/EFF、DLC、版本、保留位与语义；
4. `test_can_v1` 对照 golden vectors 做编解码往返；
5. 截断/脏帧返回稳定 `Rejected`，不抛异常；
6. `docs/KNOWLEDGE_BASE.md` §4.4 / §8.3 / §10.6 知识卡。

退出条件：golden vectors 全通过；非法帧得到明确错误。

### P3：实现独立 `rcr_node_sim`

输入：codec、SocketCan native handle、模拟器命令行参数。  
输出：只经 CAN 通信的独立 Linux 进程。

最小参数：CAN 接口、node id、heartbeat period、运行时长；故障参数单独分组且默认关闭。
模拟器周期发送 heartbeat/status，接收并校验 output command，只对当前 session 和新鲜
sequence 应用普通输出，再发送 output status。重启必须生成新的 boot/session；旧命令不重放。

首批故障场景：停止 heartbeat、延迟响应、重启换 session、重复/倒退 sequence、非法帧。
每个场景必须可由固定参数重复，不依赖人工抢时机。

知识交付物重点：进程与线程、fd/RAII、阻塞与非阻塞、epoll/timerfd、关闭顺序，以及
“vcan 走了哪些内核路径、没有模拟哪些物理行为”。

退出条件：模拟器没有 Runtime Core 状态机依赖；SIGINT 或限定时长到达后有界关闭 fd；
正常模式没有 Fault Injection 入口被意外启用。

### P4：建立真实 vcan 进程验收

输入：预先由运维脚本创建的 `vcan0`、模拟器和验收进程。  
输出：可重复的阶段 1 证据。

自动化场景：

1. heartbeat/status 正常接收；
2. output command → output status 双向闭环；
3. 重复、倒退、过期和旧 session 命令被拒绝；
4. heartbeat 中断被检测；
5. 节点重启产生新 session，恢复后不应用旧命令；
6. 错误 DLC、未知版本和非法 flags 被拒绝并计数。

知识交付物重点：集成测试与单元测试的区别、进程隔离、可重复故障注入、证据元数据，
以及面试中怎样准确区分仿真证据、ThinkPad 证据和 Orange Pi/实物证据。

退出条件：至少两个独立进程，进程间只经过 `vcan0`；缺少 `vcan0` 时验收失败而不是假通过；
连续重复运行不残留进程或 fd；结果记录内核、编译器、commit、接口和场景参数。

## 5. 建议提交边界

为便于审查和回退，按以下边界提交，不把整阶段压成一个 commit：

1. `core: harden CAN interface setup and test semantics`；
2. `protocol: freeze CAN V1 wire contract and vectors`；
3. `linux: implement CAN V1 codec`；
4. `sim: add epoll-driven CAN node simulator`；
5. `test: add repeatable vcan process scenarios`。

当前所有文件尚未跟踪。开始 G0 前应先由用户确认当前内容并建立基线 commit；本计划不代替
该确认，也不自动提交或推送。

## 6. 下一阶段之后

只有 P4 通过后才进入 `rcrd`：届时 I/O 线程拥有 SocketCAN、eventfd、signalfd 和
EpollReactor，解码后的输入/故障进入有界事件队列，周期线程继续只做监督。事件队列在
真实生产者和消费者同时存在时实现，不在本阶段为未来用途先造空抽象。

阶段 2 完成后再做 sanitizer 固化、故障矩阵、benchmark 报告和 systemd；Orange Pi
证据不能由 ThinkPad 结果替代。
