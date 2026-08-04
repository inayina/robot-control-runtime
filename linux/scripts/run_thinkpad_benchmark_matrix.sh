#!/usr/bin/env bash
# ThinkPad wrapper：调用共享 12 格矩阵，输出到 evidence/thinkpad_baseline/。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export RCR_BENCH_PLATFORM=thinkpad
export RCR_BENCH_OUT_ROOT="${ROOT}/evidence/thinkpad_baseline"
exec "${ROOT}/linux/scripts/run_benchmark_matrix.sh"
