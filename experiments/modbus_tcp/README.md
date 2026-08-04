# Modbus TCP 实验（零采购）

独立 CMake 工程，**不属于** `linux/` Runtime V1。目标：手写 MBAP/PDU、
TCP 组帧、localhost reference server，并在有 `libmodbus` 时做交叉验证。

协议笔记：[`docs/MODBUS_TCP_NOTES.md`](../../docs/MODBUS_TCP_NOTES.md)

## 构建

```bash
cmake -S experiments/modbus_tcp -B build/modbus_tcp
cmake --build build/modbus_tcp -j
ctest --test-dir build/modbus_tcp --output-on-failure
```

可选依赖：完整 `ctest`（含 `test_libmodbus_interop`）需要系统 `libmodbus-dev`。
CMake 以 `pkg-config libmodbus` **REQUIRED** 配置；缺包则整工程配置失败，而不是静默 Skip。

```bash
sudo apt install libmodbus-dev
cmake -S experiments/modbus_tcp -B build/modbus_tcp
cmake --build build/modbus_tcp -j
ctest --test-dir build/modbus_tcp --output-on-failure
```

## 手动跑

```bash
# 终端 A
./build/modbus_tcp/mbus_ref_server --port 1502

# 终端 B
./build/modbus_tcp/mbus_demo_client --host 127.0.0.1 --port 1502
```

demo 顺序：`0x06` 写 addr 10=`0xBEEF` → `0x03` 读 addr 0 qty 4 → `0x10` 写 addr 20 三个寄存器。  
server seed：`holding[0]=0x1234`、`holding[1]=0xABCD`。

## 抓包对照（操作步骤）

另开终端，在 client 跑之前启动抓包：

```bash
sudo tcpdump -i lo -nn -X -s0 'tcp port 1502'
# 或落盘（目录见 evidence/modbus_tcp/README.md；pcap 被 gitignore）：
# sudo tcpdump -i lo -nn -s0 -w evidence/modbus_tcp/capture_demo.pcap 'tcp port 1502'
```

无 root 时打印期望布局与流程（不抓包）：

```bash
./experiments/modbus_tcp/scripts/run_tcpdump_demo.sh --dry-run
```

有 sudo 时可一键打印说明并尝试抓包（仍须另开 server/client，或按脚本提示操作）：

```bash
sudo ./experiments/modbus_tcp/scripts/run_tcpdump_demo.sh
```

在 `-X` 输出里对齐：

| 字段 | 字节 | 对照 |
|---|---|---|
| TransID | 2 | 请求=响应；demo 从 1 起 |
| ProtoID | 2 | `00 00` |
| Length | 2 | 后续 Unit+PDU 长度 |
| UnitID | 1 | 默认 `00` |
| FC | 1 | `03` / `06` / `10` |
| 寄存器 | 2×N | 大端；如 `BE EF`、`12 34` |

详细期望 PDU 与日志对照见 [`docs/MODBUS_TCP_NOTES.md`](../../docs/MODBUS_TCP_NOTES.md)「抓包对照」。  
**不要**把一次本机抓包写成正式 Gate；自动化以 `ctest` 为准。

## 合同摘要

- 每连接 outstanding request = 1；
- Holding registers 零基；默认 map 大小 64；
- client 支持 connect/response timeout 与断线指数退避重连；
- 非法 length / transaction 不匹配 / exception 有自动化用例。

## 不能声称

教学 codec ≠ 生产协议栈；localhost ≠ 现场设备；未做安全分区的 TCP 不可暴露公网。
