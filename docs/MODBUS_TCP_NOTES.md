# Modbus TCP 协议笔记（experiments）

状态：Living（localhost **使用过**；双机 Wi-Fi LAN **演示过**：OPi client → ThinkPad
`:1502`；`libmodbus` 互操作仍以本机 `ctest` 为准）
规范入口：

- [Modbus Application Protocol V1.1b3](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [Modbus Messaging on TCP/IP Implementation Guide V1.0b](https://www.modbus.org/docs/Modbus_Messaging_Implementation_Guide_V1_0b.pdf)
  （若官方链失效，以 modbus.org 当前 PDF 为准）
- 路线图：`docs/plans/DEVELOPMENT_ROADMAP.md` §9
- 知识库预习：`docs/KNOWLEDGE_BASE.md` §6.11
- 与 EtherCAT 对照直觉：`docs/ETHERCAT_PROTOCOL_NOTES.md` §11（预习）

## 给初学者的一句话

Modbus 是**主站问、从站答**的寄存器读写合同。TCP 上要把“一问一答”装进字节流：
**MBAP** 管定界与事务配对，**PDU** 管“读/写哪几个 Holding”。本仓实验只做 Holding，
不进 Runtime 1 ms 闭环。

## 源码阅读顺序

```text
types.hpp          常量、MBAP/ADU、Error
  → codec          结构体 ↔ 字节（含 exception）
  → framing        TCP 半包/粘包 → 完整 ADU
  → register_map   Holding 映像 + 请求→响应/异常
  → client/server  outstanding=1、timeout、重连
  → apps/          demo 三笔对照
```

对照文件：`experiments/modbus_tcp/include|src|apps/`。

## 四类数据

| 类型 | 访问 | 本实验 |
|---|---|---|
| Coil | 位可写 | **未实现** |
| Discrete Input | 位只读 | **未实现** |
| Input Register | 16-bit 只读 | **未实现** |
| Holding Register | 16-bit 可读写 | **实现**（零基地址） |

文档里的 `4xxxx` 是厂商习惯编号，**不是**线上零基地址。本实验 Holding 地址 `0..N-1`。

## MBAP + PDU

```text
ADU = MBAP (7B) + PDU
MBAP: TransID(2) | ProtoID(2)=0 | Length(2) | UnitID(1)
Length = UnitID + PDU 的字节数
ADU 总长 = 6 + Length   （前 6 字节 + Length 所指内容）
```

TCP 是流：必须按 Length 组帧；`recv` **半包/粘包都是正常情况**，不是 bug。

本实验约定 **outstanding=1**（发完等响应再发下一笔），这样 TransID 匹配直观；并发流水线刻意不做。

## 本实验 function code

| Code | 含义 |
|---|---|
| `0x03` | Read Holding Registers |
| `0x06` | Write Single Register |
| `0x10` | Write Multiple Registers |
| `0x83`/`0x86`/`0x90` | 对应 exception（function \| 0x80） |

常用 exception：`0x01` Illegal Function，`0x02` Illegal Data Address，`0x03` Illegal Data Value。  
客户端先看 PDU[0] 最高位，再解码正常响应布局。

失败要分清：**网络/超时/断线** vs **从站 exception（明确拒绝）**。本实验仅对 `Closed`/`Io` 做指数退避重连并重发该事务；exception 与 timeout **不**静默自动重放写。

寄存器线上为大端 `uint16`。多寄存器拼 float/32-bit **不做**协议统一约定。

## 抓包对照（tcpdump / Wireshark）

目标：把 `tcpdump -X` 里的十六进制与 MBAP/PDU 合同、以及 demo 进程日志对上号。  
这是**操作步骤**，不是已入库的现场抓包证据；本仓未把 pcap 当作正式 Gate。

### 准备

```bash
# 已构建前提下：
# 终端 A
./build/modbus_tcp/mbus_ref_server --port 1502

# 终端 B（先不跑；等抓包就绪）
# ./build/modbus_tcp/mbus_demo_client --host 127.0.0.1 --port 1502

# 终端 C：抓 lo + 1502
sudo tcpdump -i lo -nn -X -s0 'tcp port 1502'
# 或写入文件后再用 Wireshark / tshark 看：
# sudo tcpdump -i lo -nn -s0 -w evidence/modbus_tcp/capture_$(date -u +%Y%m%dT%H%M%SZ).pcap 'tcp port 1502'
```

无 `sudo` / 无 `CAP_NET_RAW`：tcpdump 通常失败。可用  
`experiments/modbus_tcp/scripts/run_tcpdump_demo.sh --dry-run` 打印流程与期望字节布局。  
自动化证据仍以 `ctest` 为准，不依赖抓包。

可选封装：`./experiments/modbus_tcp/scripts/run_tcpdump_demo.sh`（需 root 才真正抓包）。

### 在 hex 里找什么

TCP payload 从 MBAP 开始（跳过以太网/IP/TCP 头；`-X` 打印整帧时看 TCP 段末尾的应用数据）：

| 偏移 | 字段 | 期望 |
|---|---|---|
| 0–1 | Transaction ID | 请求/响应相同；`mbus_demo_client` 从 1 递增（跳过 0） |
| 2–3 | Protocol ID | `00 00` |
| 4–5 | Length | Unit ID + PDU 字节数（大端） |
| 6 | Unit ID | `ClientConfig` 默认 `01`；本教学 server 忽略 Unit 仅回显 |
| 7 | Function | `03` / `06` / `10`；异常为 `83`/`86`/`90` |
| 8… | Data | 地址、数量、寄存器值均为 **big-endian `uint16`** |

`mbus_demo_client` 依次发三笔（outstanding=1），可按 TransID 对齐：

1. **FC `0x06` Write Single**：addr=`10`=`0x000A`，value=`0xBEEF`  
   请求 PDU：`06 00 0A BE EF`；Length=`00 06`  
   响应 PDU 回显相同五字节。
2. **FC `0x03` Read Holding**：addr=`0`，qty=`4`  
   请求 PDU：`03 00 00 00 04`；Length=`00 06`  
   响应：`03 08` + 8 字节寄存器。server seed 为 `holding[0]=0x1234`、`[1]=0xABCD`，其余默认 0 →  
   数据段常见：`12 34 AB CD 00 00 00 00`（以你本机 seed 为准）。
3. **FC `0x10` Write Multiple**：addr=`20`=`0x0014`，qty=`3`，bytes=`6`，值 `1,2,3`  
   请求 PDU：`10 00 14 00 03 06 00 01 00 02 00 03`；Length=`00 0D`  
   响应 PDU：`10 00 14 00 03`；Length=`00 06`。

客户端成功时 stdout 类似：

```text
read holding[0..3]: 0x1234 0xabcd 0x0 0x0
write_multiple ok addr=20 qty=3
```

把同一 TransID 的请求/响应 hex 与上表、日志三行对照即可。Wireshark 可滤 `tcp.port == 1502`；若装了 Modbus 解析器会标出 Transaction/Protocol/Length/Unit/Function。未装时按上表手解。

### 不能从抓包夸大的事

- localhost `lo` 抓包 ≠ LAN / 现场设备互操作证据；
- 一次成功 hex 对照 ≠ 生产协议栈或安全分区已完成；
- pcap 默认 gitignore，不作为本阶段正式 Gate 产物。

## 与本仓边界

- 不进入 `linux/` Runtime / 1 ms 闭环；
- 不抽取与 SocketCAN 共用的通用 Transport；
- 学习端口 **1502**（非特权）。
