# CAN V1 协议边界

V1 只围绕 SocketCAN 和 `vcan0`，不建立 UART/CAN/Modbus 通用 Transport。协议对端是
独立 CAN 节点模拟器；未来物理 CAN 复用经过测试的线级合同。

```text
protocol/
└── can_v1/
    ├── README.md           冻结的线级合同（权威）
    └── golden_vectors.tsv  与 README §10 同步的向量表
```

线级合同状态：**Frozen（protocol_version = 1）**。权威文本见
[can_v1/README.md](can_v1/README.md)。

已冻结内容：

- 经典 CAN 8-byte、标准 11-bit ID、大端整数；
- 四类消息的 ID 窗、字段布局、单位、保留位与非法值行为；
- boot/session 生成规则、u16 序号回绕比较、相对有效期换算；
- heartbeat 周期/超时与总线负载粗算；
- golden vectors（最小 / 典型 / 边界 / 非法）。

尚未实现（属于后续工作包，不阻塞合同与 codec）：

- `rcr_node_sim` 与双进程 vcan 验收（P3/P4）。

Linux 侧显式 encode/decode 已实现：`rcr::can_v1`（`linux/include/rcr/can_v1.hpp`），
由 `test_can_v1` 对照 golden vectors 验证。

约束仍生效：

- 显式编解码，不发送编译器内存布局；
- Runtime 与模拟器运行同一组 golden vectors；
- 输入边沿、故障和状态事件不能通过 latest-wins 邮箱静默覆盖；
- Fault Injection 使用模拟器启动参数，默认关闭，不占用正式 CAN 消息。

Linux 内部 `rcr::OutputCommand` 不是共享线协议结构，不能直接复制到 CAN payload。
