#!/usr/bin/env python3
"""LD4 fixture tests: deterministic output and malformed-input failure."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / "linux" / "scripts" / "diagnostics"
FIXTURES = TOOLS / "tests" / "fixtures"


class DiagnosticsTest(unittest.TestCase):
    def run_script(self, script: str, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(TOOLS / script), *args], text=True, capture_output=True, check=check)

    def test_final_summary_and_bad_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "final.json"
            self.run_script("parse_runtime_trace.py", "--input", str(FIXTURES / "rcrd_final.log"), "--output", str(output))
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["values"]["mode"], "IDLE")
            bad = Path(temp) / "bad.log"
            bad.write_text("not a final summary\n", encoding="utf-8")
            self.assertEqual(self.run_script("parse_runtime_trace.py", "--input", str(bad), "--output", str(Path(temp) / "bad.json"), check=False).returncode, 2)

    def test_summary_timeline_and_compare(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run = root / "run"
            shutil.copytree(FIXTURES / "ld5_run", run)
            first, second = root / "first.json", root / "second.json"
            self.run_script("summarize_run.py", "--run-dir", str(run), "--output", str(first))
            self.run_script("summarize_run.py", "--run-dir", str(run), "--output", str(second))
            self.assertEqual(first.read_text(encoding="utf-8"), second.read_text(encoding="utf-8"))
            self.assertEqual(json.loads(first.read_text(encoding="utf-8"))["scenario_counts"]["pass"], 5)
            timeline_json, timeline_md = root / "timeline.json", root / "timeline.md"
            self.run_script("build_incident_timeline.py", "--run-dir", str(run), "--output-json", str(timeline_json), "--output-markdown", str(timeline_md))
            self.assertIn("runner order", timeline_md.read_text(encoding="utf-8"))
            compare_json, compare_md = root / "compare.json", root / "compare.md"
            self.run_script("compare_runs.py", "--left", str(first), "--right", str(second), "--output-json", str(compare_json), "--output-markdown", str(compare_md))
            self.assertEqual(json.loads(compare_json.read_text(encoding="utf-8"))["scheduler_delta"]["deadline_misses"], 0)


if __name__ == "__main__":
    unittest.main()
