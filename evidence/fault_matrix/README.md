# 故障矩阵证据

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0   # 若尚未创建
./linux/scripts/run_fault_matrix.sh vcan0
```

缺 `vcan0` 时脚本硬失败。逐场景 `pass|failed|permission_denied|unsupported|not_run`
写入带时间戳的结果文件。
