#!/usr/bin/env python3
"""把 LD5 runner 顺序写成明确带警告的 timeline，而非伪造测量时间。"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from common import key_values, write_json
from summarize_run import results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-markdown", required=True, type=Path)
    args = parser.parse_args()
    try:
        environment = key_values(args.run_dir / "environment.txt")
        entries = [{"ordinal": index, **row} for index, row in enumerate(results(args.run_dir / "RESULTS.txt"), 1)]
        value = {"schema": "rcr.local_diagnostics.incident_timeline.v1", "anchor_utc": environment.get("date_utc", "UNKNOWN"), "classification": environment.get("classification", "UNKNOWN"), "entries": entries, "warning": "ordinal order is runner order; source has no per-scenario timestamp"}
        write_json(args.output_json, value)
        lines = ["# LD5 Incident Timeline", "", f"- Anchor UTC: `{value['anchor_utc']}`", f"- Classification: `{value['classification']}`", f"- Warning: {value['warning']}", "", "| # | Scenario | Result | Exit | Detail |", "|---:|---|---|---:|---|"]
        lines.extend(f"| {row['ordinal']} | {row['scenario']} | {row['result']} | {row['exit_code']} | {row['detail']} |" for row in entries)
        args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
        args.output_markdown.write_text("\n".join(lines) + "\n", encoding="utf-8")
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
