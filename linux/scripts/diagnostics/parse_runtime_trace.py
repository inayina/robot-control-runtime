#!/usr/bin/env python3
"""只解析 RuntimeDaemon 已输出的 final-summary 行，不把它泛化成日志框架。"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from common import write_json


def parse(path: Path) -> dict[str, object]:
    found: tuple[int, str] | None = None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if "final summary " in line:
            found = (number, line.split("final summary ", 1)[1])
    if found is None:
        raise ValueError(f"{path}: no RuntimeDaemon final summary")
    number, payload = found
    values: dict[str, object] = {}
    for token in payload.split():
        if "=" not in token:
            raise ValueError(f"{path}:{number}: malformed token {token!r}")
        key, value = token.split("=", 1)
        values[key] = int(value) if re.fullmatch(r"-?[0-9]+", value) else value
    return {"schema": "rcr.runtime_final_summary.v1", "source_line": number, "values": values}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        write_json(args.output, parse(args.input))
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
