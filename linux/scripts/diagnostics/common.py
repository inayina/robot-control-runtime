#!/usr/bin/env python3
"""LD4 的最小文件 I/O：只读取落盘 evidence，不连接 Runtime。"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def key_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{number}: expected key=value")
        key, value = line.split("=", 1)
        if not key:
            raise ValueError(f"{path}:{number}: empty key")
        values[key] = value
    return values


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
