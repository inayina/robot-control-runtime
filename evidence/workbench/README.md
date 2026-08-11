# Workbench 软件证据

使用以下命令从**干净提交**生成 Phase 3.5 证据：

```bash
linux/scripts/run_workbench_clean_evidence.sh vcan0
```

脚本要求工作树在开始时 clean，并记录：环境、`vcan0` 状态、全量 CTest、Workbench
ASan/UBSan、正常 heartbeat、停止 heartbeat、非法帧三种 CAN Health 结果及 SHA-256。

时间戳目录受 `.gitignore` 管理，避免把每次本地复跑的大量原始日志提交进仓库；需要公开时，
在 `evidence/portfolio/` 提交脱敏摘要并保留原始目录路径和哈希。

这里的 `VCAN` 只证明 Linux/SocketCAN 软件路径，不证明 physical CAN、真实 MCU、执行器、
电气质量、硬实时或功能安全。
