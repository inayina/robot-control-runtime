# evidence/realtime_linux

Real-time Linux Lab 正式证据根目录（合同见
[`docs/REALTIME_EVIDENCE_SCHEMA.md`](../../docs/REALTIME_EVIDENCE_SCHEMA.md)）。

## 状态

- **RT0**：字段/分类/任务模型已冻结；不创建假报告。
- **RT1/RT2**：smoke + cyclictest 归因见作品集摘要（dirty experiment）。
- **RT3**：用户态夹具 `experiments/realtime_userspace/`；本机证据
  `*_rt3_userspace/`（gitignore 大样）；摘要见 portfolio。

历史 Orange Pi 5 秒 Debug 矩阵保留为 pilot：

- 脱敏摘要：[`../portfolio/orangepi_scheduler_20260805.txt`](../portfolio/orangepi_scheduler_20260805.txt)
- 本地原文：`../orangepi_baseline/20260805T085844Z/`（默认不入库）

正式样本目录：

```text
evidence/realtime_linux/<stamp>_orangepi_rt1_<smoke|formal>/
├── environment.txt
├── command.txt
├── topology.txt
├── summary.txt
├── stderr.txt
├── SHA256SUMS
└── cells/<cell_id>/...
```

`pilot` / `smoke` 不得与 `baseline` 混写提升百分比。缺少 `stress-ng` 时压力格记
`unsupported`，不能降级成 idle PASS。formal 要求 `git_dirty=false`。
