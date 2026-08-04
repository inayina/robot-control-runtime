# ThinkPad 证据基线（P2）

本目录由脚本生成，默认被 `.gitignore` 忽略。正式入库需在干净 commit 上采集后
显式挑选 SUMMARY/environment。

## 生成

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug && cmake --build build/linux -j
./linux/scripts/run_asan_ubsan.sh
./linux/scripts/run_tsan.sh
./linux/scripts/run_fault_matrix.sh vcan0
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_thinkpad_benchmark_matrix.sh
```

Schema：[`docs/EVIDENCE_SCHEMA.md`](../../docs/EVIDENCE_SCHEMA.md)

## 解读边界

- 空 callback 的 lateness **不是** CAN/控制延迟；
- `unsupported`（缺 stress-ng）与 `permission_denied`（FIFO）不是代码 PASS；
- ThinkPad 数据是 Orange Pi 对照，不是部署证据，也不是硬实时证明。
