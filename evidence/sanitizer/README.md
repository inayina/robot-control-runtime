# sanitizer 证据

```bash
./linux/scripts/run_asan_ubsan.sh   # 独立 build/linux-asan；LSan 默认关闭并记录
./linux/scripts/run_tsan.sh         # 独立 build/linux-tsan；mapping 失败记 unsupported
```

`result=pass|failed|unsupported` 不得混用。ASan PASS 不包含泄漏证明（若报告写明
`lsan=disabled`）。
