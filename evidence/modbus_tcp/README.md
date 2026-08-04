# Modbus TCP 实验证据说明

自动化结果以本机 `ctest --test-dir build/modbus_tcp` 为准，不在此目录存二进制测试日志。

合同与用法：

- 协议笔记（含抓包对照）：`docs/MODBUS_TCP_NOTES.md`
- 工程：`experiments/modbus_tcp/`
- 抓包脚本：`experiments/modbus_tcp/scripts/run_tcpdump_demo.sh`
- 知识库：`docs/KNOWLEDGE_BASE.md` §6.11 / §10.12

## 抓包产物（非正式 Gate）

可选把 `tcpdump -w` 的 `.pcap` 放在本目录做本机对照，例如：

```bash
sudo tcpdump -i lo -nn -s0 -w evidence/modbus_tcp/capture_demo.pcap 'tcp port 1502'
```

`.gitignore` 忽略 `/evidence/**` 下除 README 外的内容，**pcap/原文不会入库**。  
一次 localhost hex 对照**不是**正式 Gate；不要用本目录声称现场/LAN/安全分区证据。

## 已覆盖（localhost · 以 ctest 为准）

- hand-rolled codec / 半包粘包组帧
- reference server ↔ client（0x03/0x06/0x10 + exception）
- 我方 ↔ libmodbus 双向互操作（需 `libmodbus-dev`）
- timeout / reconnect / illegal length / transaction mismatch

## 未覆盖

双机 LAN、Modbus RTU、生产安全分区、接入 Runtime Core、入库的抓包 Gate。
