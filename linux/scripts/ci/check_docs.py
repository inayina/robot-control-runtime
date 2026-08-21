#!/usr/bin/env python3
"""Run local-only document and traceability checks for LD7.

This checker deliberately validates links and the LD6 matrix shape only. It does
not fetch URLs, execute deployment actions, or infer Runtime capability from
documentation.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[3]
LINK_RE = re.compile(r"!?\[[^]]*\]\(([^)]+)\)")
SKIP_PREFIXES = ("http://", "https://", "mailto:", "data:")


def markdown_files() -> list[Path]:
    candidates = [ROOT / "README.md", ROOT / "AGENTS.md"]
    candidates.extend((ROOT / "docs").rglob("*.md"))
    candidates.extend((ROOT / "deploy").rglob("*.md"))
    return sorted({path for path in candidates if path.is_file()})


def check_links() -> list[str]:
    errors: list[str] = []
    link_count = 0
    documents = markdown_files()
    for document in documents:
        text = document.read_text(encoding="utf-8")
        for raw_target in LINK_RE.findall(text):
            target = raw_target.strip().split("#", 1)[0]
            if not target or target.startswith("#") or target.startswith(SKIP_PREFIXES):
                continue
            target = unquote(target).strip("<>")
            link_count += 1
            resolved = (document.parent / target).resolve()
            if not resolved.exists():
                errors.append(f"missing relative link: {document.relative_to(ROOT)} -> {target}")
    print(f"markdown_files={len(documents)} relative_links={link_count}")
    return errors


def check_matrix() -> list[str]:
    errors: list[str] = []
    matrix_path = ROOT / "docs" / "REQUIREMENTS_TRACEABILITY_MATRIX.md"
    text = matrix_path.read_text(encoding="utf-8")
    rows = [line for line in text.splitlines() if re.match(r"^\| `REQ-00[1-6]`", line)]
    if len(rows) != 6:
        errors.append(f"traceability matrix must contain 6 requirement rows, got {len(rows)}")
    for line in rows:
        if line.count("|") != 8:
            errors.append("traceability matrix row does not have seven cells")
    if "`REQ-003`" in text and "PARTIAL / NOT_RUN" not in text:
        errors.append("REQ-003 must retain PARTIAL / NOT_RUN")
    return errors


def main() -> int:
    errors = check_links()
    try:
        json.loads((ROOT / "linux" / "CMakePresets.json").read_text(encoding="utf-8"))
        print("cmake_presets=json_valid")
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"invalid linux/CMakePresets.json: {exc}")
    errors.extend(check_matrix())
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    print("document_checks=pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
