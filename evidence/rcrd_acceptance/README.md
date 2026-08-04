# rcrd P1 验收证据

日期：2026-08-01  
主机：ThinkPad（`ina-Gen6`）+ `vcan0`  
结论：P1 Gate 在本机通过；**不**声称 systemd、Orange Pi/ARM 或硬实时已验证。

## 结果摘要

| 项 | 结果 |
|---|---|
| CTest（17） | 全部 Passed |
| 服务级 `test_runtime_daemon` | 在线、CommLoss、重复 start/stop |
| 进程级 `test_rcrd_process` | `--help`、缺接口退出码 2、SIGTERM、duration、重复启动 |
| `rcrd --duration-ms 30` ×100 | `ok=100 fail=0` |
| SIGTERM | `exit=0`，`io stopped reason=SIGNAL` |

## 文件

- `environment.txt` — 内核、compiler、commit、dirty、vcan0
- `ctest.txt` — 完整 CTest 输出
- `daemon_service.txt` / `daemon_process.txt` — 可选手动复跑留档
- `repeat_100.txt` — 100 次 duration 启动
- `sigterm.txt` — 独立进程 SIGTERM

## 复跑

```bash
sudo ./linux/scripts/setup_vcan.sh vcan0
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
./build/linux/rcrd --can vcan0 --duration-ms 500
```

正式基线应在干净 commit 上采集；本目录生成时工作树可能仍含未提交 P1 变更，
以 `environment.txt` 的 `git_dirty_lines` 为准。
