# evidence/orangepi_baseline

Orange Pi 12 格调度/负载矩阵输出目录（与 ThinkPad 同 schema）。

由 `./linux/scripts/run_orangepi_benchmark_matrix.sh` 写入
`evidence/orangepi_baseline/<stamp>/`。

在 ThinkPad 上跑该 wrapper 只验证脚本通路，**不是** ARM 基线证据。
正式对照须在 aarch64 板上、与 ThinkPad 同 commit / 同 `RCR_BENCH_DURATION_MS` 条件下采集。

共享循环体：`linux/scripts/run_benchmark_matrix.sh`。
Schema：[`docs/EVIDENCE_SCHEMA.md`](../../docs/EVIDENCE_SCHEMA.md)。
