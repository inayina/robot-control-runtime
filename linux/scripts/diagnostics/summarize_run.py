#!/usr/bin/env python3
"""汇总一个 LD5 batch；只报告输入事实，不推断物理或因果结论。"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from common import key_values, write_json


def results(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        prefix, marker, detail = line.partition(" detail=")
        if not marker:
            raise ValueError(f"{path}:{number}: missing detail")
        fields = dict(token.split("=", 1) for token in prefix.split() if "=" in token)
        fields["detail"] = detail
        if not {"scenario", "result", "exit_code", "detail"} <= fields.keys():
            raise ValueError(f"{path}:{number}: incomplete scenario result")
        try:
            code = int(fields["exit_code"])
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid exit_code") from exc
        rows.append({"scenario": fields["scenario"], "result": fields["result"], "exit_code": code, "detail": fields["detail"]})
    if not rows:
        raise ValueError(f"{path}: no scenario result")
    return rows


def summarize(run_dir: Path) -> dict[str, object]:
    environment = key_values(run_dir / "environment.txt")
    scenarios = results(run_dir / "RESULTS.txt")
    benchmark_path = run_dir / "05_scheduler_overload" / "benchmark_stdout.txt"
    benchmark = key_values(benchmark_path) if benchmark_path.is_file() else {}
    metrics = ("cycles", "deadline_misses", "lateness_mean_ns", "lateness_p50_ns", "lateness_p95_ns", "lateness_p99_ns", "lateness_max_ns")
    warnings = []
    if "PHYSICAL" not in environment.get("classification", "UNKNOWN"):
        warnings.append("local_evidence_not_physical_acceptance")
    if environment.get("git_dirty") == "true":
        warnings.append("dirty_worktree")
    return {
        "schema": "rcr.local_diagnostics.run_summary.v1",
        "environment": {key: environment.get(key) for key in ("date_utc", "git_commit", "git_dirty", "classification", "os_kernel", "machine")},
        "scenario_counts": {"total": len(scenarios), "pass": sum(row["result"] == "pass" for row in scenarios), "failed": sum(row["result"] != "pass" for row in scenarios)},
        "scenarios": scenarios,
        "scheduler": {key: benchmark[key] for key in metrics if key in benchmark},
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        write_json(args.output, summarize(args.run_dir))
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
