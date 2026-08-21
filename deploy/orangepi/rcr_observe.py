#!/usr/bin/env python3
"""Emit the local read-only observability projection as stable JSON.

The command has no control path.  It reads current/MANIFEST, asks systemd for
process metadata, and optionally performs CEL1 GetStatus through the existing
read-only probe.  Missing sources remain explicit null/UNKNOWN values.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

from cel1_status_probe import probe


SCHEMA = "rcr.local_observability.v1"


def unknown(reason: str) -> dict[str, object]:
    return {"availability": "UNKNOWN", "reason": reason, "values": {}}


def manifest(path: Path) -> dict[str, object]:
    if not path.is_file():
        return unknown("manifest_missing")
    values: dict[str, str] = {}
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value
    except OSError as exc:
        return unknown(f"manifest_read_error:{exc}")
    return {"availability": "AVAILABLE", "owner": "release_manifest", "values": values}


def systemd(unit: str) -> dict[str, object]:
    if shutil.which("systemctl") is None:
        return unknown("systemctl_missing")
    try:
        active = subprocess.run(
            ["systemctl", "is-active", unit],
            text=True,
            capture_output=True,
            check=False,
            timeout=1.0,
        )
        shown = subprocess.run(
            ["systemctl", "show", "-p", "MainPID", "-p", "ActiveEnterTimestampMonotonic", unit],
            text=True,
            capture_output=True,
            check=False,
            timeout=1.0,
        )
    except subprocess.TimeoutExpired:
        return unknown("systemd_query_timeout")
    except OSError as exc:
        return unknown(f"systemd_query_error:{exc}")
    if active.returncode != 0 and shown.returncode != 0:
        return unknown("systemd_source_unavailable")
    values: dict[str, object] = {
        "service_active": active.stdout.strip() if active.returncode == 0 else "UNKNOWN",
    }
    for line in shown.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            if key == "MainPID":
                try:
                    pid = int(value)
                except ValueError:
                    pid = 0
                values["main_pid"] = pid
                values["process_alive"] = pid > 0 and Path(f"/proc/{pid}").is_dir()
            elif key == "ActiveEnterTimestampMonotonic":
                values["active_enter_monotonic_us"] = int(value) if value.isdigit() else None
    values.setdefault("main_pid", None)
    values.setdefault("process_alive", None)
    return {"availability": "AVAILABLE", "owner": "systemd+kernel", "values": values}


def local_age(observed_ns: object, host: str, now_ns: int) -> object:
    if host not in {"127.0.0.1", "localhost", "::1"}:
        return None
    if not isinstance(observed_ns, int) or observed_ns < 0:
        return None
    age = now_ns - observed_ns
    return age if age >= 0 else None


def runtime_observation(host: str, port: int, timeout: float, now_ns: int) -> dict[str, object]:
    try:
        values = probe(host, port, timeout)
    except (OSError, RuntimeError, ValueError, struct.error) as exc:
        return {
            "availability": "UNAVAILABLE",
            "owner": "rcr_cell_app/CEL1",
            "source": {"host": host, "port": port},
            "error": str(exc),
            "sample_age_ns": None,
            "age_semantics": "UNKNOWN",
            "values": {},
        }
    return {
        "availability": "AVAILABLE",
        "owner": "rcr_cell_app/CEL1",
        "source": {"host": host, "port": port},
        "observed_monotonic_ns": values.get("observed_monotonic_ns"),
        "sample_age_ns": local_age(values.get("observed_monotonic_ns"), host, now_ns),
        "age_semantics": "same_host_CLOCK_MONOTONIC" if host in {"127.0.0.1", "localhost", "::1"} else "UNKNOWN_REMOTE_CLOCK_DOMAIN",
        "values": values,
    }


def runtime_unavailable(reason: str) -> dict[str, object]:
    return {
        "availability": "UNAVAILABLE",
        "owner": "LinuxRuntime/CEL1 boundary",
        "source": {},
        "error": reason,
        "sample_age_ns": None,
        "age_semantics": "UNKNOWN",
        "values": {},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", default="/opt/robot-control-runtime")
    parser.add_argument("--service", default="rcr-cell-app.service")
    parser.add_argument("--cell-host", default="127.0.0.1")
    parser.add_argument("--cell-port", type=int, default=5750)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--expected-release", default="")
    args = parser.parse_args()
    prefix = Path(args.prefix)
    if not prefix.is_absolute():
        print("error: prefix must be absolute", file=sys.stderr)
        return 1
    now_ns = time.monotonic_ns()
    collected = dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")
    current = prefix / "current"
    current_id = current.resolve().name if current.is_symlink() else None
    release = manifest(current / "MANIFEST.txt")
    if current_id is None:
        match = None
    elif args.expected_release:
        match = current_id == args.expected_release
    elif release.get("availability") == "AVAILABLE":
        # 与 healthcheck 使用同一语义：current 链接存在不等于 MANIFEST 属于该 release。
        release_id = release.get("values", {}).get("release_id")
        match = current_id == release_id if release_id else None
    else:
        match = None
    service = systemd(args.service)
    runtime = (
        runtime_observation(args.cell_host, args.cell_port, args.timeout, now_ns)
        if args.service == "rcr-cell-app.service"
        else runtime_unavailable("no_read_only_endpoint_for_unit")
    )
    errors = []
    if release.get("availability") != "AVAILABLE":
        errors.append("release_manifest_unavailable")
    if service.get("availability") != "AVAILABLE":
        errors.append("service_source_unavailable")
    if runtime.get("availability") != "AVAILABLE":
        errors.append("runtime_source_unavailable")
    result = {
        "schema": SCHEMA,
        "collected_utc": collected,
        "collector_monotonic_ns": now_ns,
        "host": {
            "owner": "kernel/operations",
            "values": {"hostname": platform.node(), "kernel": platform.release(), "arch": platform.machine()},
        },
        "release": {
            **release,
            "current_id": current_id,
            "version_match": match,
        },
        "service": {**service, "unit": args.service},
        "runtime": runtime,
        "errors": errors,
    }
    print(json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
