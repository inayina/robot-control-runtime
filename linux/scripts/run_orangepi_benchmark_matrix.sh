#!/usr/bin/env bash
# Orange Pi wrapper：调用同一套 12 格矩阵，输出到 evidence/orangepi_baseline/。
# 到货后在 aarch64 板上、与 ThinkPad 同 commit/同 duration 条件下执行。
# 本脚本本身不是 ARM 实测证据；跑出的报告才是。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export RCR_BENCH_PLATFORM=orangepi
export RCR_BENCH_OUT_ROOT="${ROOT}/evidence/orangepi_baseline"
exec "${ROOT}/linux/scripts/run_benchmark_matrix.sh"
