# LD4 Offline Diagnostics

本目录只读取已经导出的 evidence/log/JSON；不连接 Runtime、不打开 CAN/串口，也不把分析结果写回控制路径。

| Script | Input | Output |
|---|---|---|
| `parse_runtime_trace.py` | `RuntimeDaemon` 的 `final summary` 日志行 | versioned JSON |
| `summarize_run.py` | LD5 `environment.txt`、`RESULTS.txt`、可选 benchmark | versioned JSON |
| `build_incident_timeline.py` | LD5 results 与 UTC anchor | JSON + Markdown |
| `compare_runs.py` | 两份 run summary | JSON + Markdown |

```bash
run=evidence/ld5_incidents/20260818T141251Z
out=/tmp/rcr-ld4
python3 linux/scripts/diagnostics/summarize_run.py --run-dir "$run" --output "$out/summary.json"
python3 linux/scripts/diagnostics/build_incident_timeline.py --run-dir "$run" \
  --output-json "$out/timeline.json" --output-markdown "$out/timeline.md"
```

timeline 只有 runner 顺序；没有每场景时间戳时会警告，不能虚构时序因果。当前小型固定 schema 用标准库即可，不为名称强加 pandas 依赖。验证：`python3 linux/scripts/diagnostics/tests/test_diagnostics.py`。
