#!/usr/bin/env python3
"""比较两份 LD4 summary 的数值差；差值不是因果或跨平台排名。"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from common import write_json


def load(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != "rcr.local_diagnostics.run_summary.v1":
        raise ValueError(f"{path}: unsupported summary schema")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", required=True, type=Path)
    parser.add_argument("--right", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-markdown", required=True, type=Path)
    args = parser.parse_args()
    try:
        left, right = load(args.left), load(args.right)
        metrics = ("deadline_misses", "lateness_mean_ns", "lateness_p50_ns", "lateness_p95_ns", "lateness_p99_ns", "lateness_max_ns")
        deltas = {}
        for metric in metrics:
            try:
                deltas[metric] = int(right["scheduler"][metric]) - int(left["scheduler"][metric])
            except (KeyError, TypeError, ValueError):
                deltas[metric] = None
        counts = {key: int(right["scenario_counts"][key]) - int(left["scenario_counts"][key]) for key in ("total", "pass", "failed")}
        value = {"schema": "rcr.local_diagnostics.compare_runs.v1", "left": left["environment"], "right": right["environment"], "scenario_count_delta": counts, "scheduler_delta": deltas, "warning": "deltas compare supplied local evidence only; they do not establish causality or cross-platform ranking"}
        write_json(args.output_json, value)
        lines = ["# LD4 Run Comparison", "", value["warning"], "", "| Metric | Delta (right - left) |", "|---|---:|"]
        lines.extend(f"| {metric} | {delta if delta is not None else 'UNKNOWN'} |" for metric, delta in deltas.items())
        args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
        args.output_markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
