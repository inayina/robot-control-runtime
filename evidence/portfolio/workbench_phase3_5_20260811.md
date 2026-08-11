# Workbench Phase 3.5 Clean Evidence

## 结论

- 状态：`pass`
- 证据等级：`VCAN` 软件链
- Git commit：`cf5892e4015bf5cd08922f290636dac1f00a3fe7`
- 工作树：`git_dirty=false`
- 原始目录：`evidence/workbench/20260811T100036Z/`（本机 ignored artifact）
- 主机：ThinkPad x86_64，Linux `7.0.0-28-generic`
- 构建：GCC 13.3.0，Debug

## Gate 结果

| Gate | 结果 | 证据 |
|---|---|---|
| 全量普通 CTest | `pass` | 23/23，0 failed，0 skipped |
| CAN Health 正常 heartbeat | `pass` | `vcan-health-e2e.json/.csv` |
| heartbeat 停止注入 | expected `FAIL` observed，case `pass` | `vcan-health-stop-hb.json/.csv` |
| 非法帧注入 | expected `FAIL` observed，case `pass` | `vcan-health-illegal.json/.csv` |
| Workbench ASan/UBSan | `pass` | 5/5；LSan 关闭 |
| Result schema | `pass` | `rcr.workbench.result.v1` |
| SHA-256 清单 | `pass` | `sha256sums.txt` |

两条故障注入用例里的 `TestResult::FAIL` 是期望的设备通信健康判定；外层自动测试观察到预期
FAIL、诊断和 cleanup 后自身 PASS。不能把二者混写。

## 不能声称

- 没有 physical CAN、真实 MCU、执行器或电气层证据；
- 没有 Qt UI、IPC 或 `Qt crash != Runtime crash` 证据；
- 没有 hard real-time 或功能安全证据；
- dirty-tree 或受限沙箱复跑不能替代本次 clean-commit 主机证据。

## 关键哈希

```text
ctest_all.txt              8b194b201af41b5333fa78ed4f9ec6ffa1bbc05f1b7c80515702cc9e7831f1e2
can_health_vcan.txt        58d53476a83030c2419f979c2859ed189c723cb1ed232bd8594de8ff8489303e
asan_ubsan.txt             120bae9ad0604eebc081554f9239a865907f94e62d284bcda7c6ff0c272ad9b0
vcan-health-e2e.json       70448c371c44b01d2099325a13d455485ecea04b5f759895c0b14f8b15700e11
vcan-health-stop-hb.json   97af7f2ace8d877a71d1d2de71baef368ef84183ef91879ba3d08e14230be1c9
vcan-health-illegal.json   8ff33115d571f074ac4862fb06aa568ffe0f3075a9c3c808139708f6bff07122
```
