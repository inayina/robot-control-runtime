# evidence/orangepi_baseline

Orange Pi 12 格调度/负载矩阵输出目录（与 ThinkPad 同 schema）。

由 `./linux/scripts/run_orangepi_benchmark_matrix.sh` 写入
`evidence/orangepi_baseline/<stamp>/`。

在 ThinkPad 上跑该 wrapper 只验证脚本通路，**不是** ARM 基线证据。
正式对照须在 aarch64 板上、与 ThinkPad 同 commit / 同 `RCR_BENCH_DURATION_MS` 条件下采集。

`20260805T085844Z`（及作品集脱敏摘要）已由 RT0 标注为 **pilot**：5 秒 Debug、CPU0/A55、
dirty tree、空 callback。它证明测量链路跑通，**不是** Real-time Lab RT1 正式基线。
正式实时样本合同见 [`docs/REALTIME_EVIDENCE_SCHEMA.md`](../../docs/REALTIME_EVIDENCE_SCHEMA.md)
与 [`../realtime_linux/`](../realtime_linux/)。

共享循环体：`linux/scripts/run_benchmark_matrix.sh`。
Schema：[`docs/EVIDENCE_SCHEMA.md`](../../docs/EVIDENCE_SCHEMA.md)。
