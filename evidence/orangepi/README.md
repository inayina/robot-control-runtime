# evidence/orangepi

Orange Pi 实机证据目录约定。已有本地填表与报告：`20260805T084028Z/`（B0–B3；B2 安装
PASS、内核 CAN 机制 unsupported、`rcrd` inactive）。ARM 矩阵在
`evidence/orangepi_baseline/`；适合公开仓库的脱敏摘要见
[`evidence/portfolio/`](../portfolio/README.md)。

## 目录

```text
evidence/orangepi/
  README.md                          # 本文件
  host_snapshot_<stamp>/             # collect_orangepi_host_snapshot.sh
    environment.txt
    board_snapshot.txt
    cpu_topology.txt
  BRINGUP_FILLED.md                  # 从 BRINGUP_CHECKLIST.md 复制后填写（可选放子目录）
  .../                               # B1/B2 手工归档：systemctl cat、journal 摘录等

evidence/orangepi_baseline/<stamp>/  # run_orangepi_benchmark_matrix.sh（与 ThinkPad 同 schema）
```

## 结果枚举

沿用 [EVIDENCE_SCHEMA.md](../../docs/EVIDENCE_SCHEMA.md)：`pass` / `failed` /
`permission_denied` / `unsupported` / `not_run`。勾选表里的 `NOT_RUN` 对应尚未执行。

## 入口

```bash
# 主机快照（自动字段 + NOT_OBSERVED 占位）
./linux/scripts/collect_orangepi_host_snapshot.sh

# 12 格矩阵（共享 runner）
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_orangepi_benchmark_matrix.sh
```

勾选表模板：[`deploy/orangepi/BRINGUP_CHECKLIST.md`](../../deploy/orangepi/BRINGUP_CHECKLIST.md)。
