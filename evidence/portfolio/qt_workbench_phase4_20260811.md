# Qt Workbench Phase 4 Clean Evidence

## 结论

- 状态：`pass`
- 证据等级：`VCAN` 软件链
- Git commit：`834ec899b9aef0ef5c1b21b392456ec28fa1d5a7`
- 工作树：`git_dirty=false`
- Qt：6.4.2，Debug
- 原始目录：`evidence/qt_workbench/20260811T102317Z/`（本机 ignored artifact）
- 主机：ThinkPad x86_64，Linux `7.0.0-28-generic`

## Gate 结果

| Gate | 结果 | 证据 |
|---|---|---|
| Qt OFF 独立构建与全量回归 | `pass` | 23/23，0 failed |
| Qt ON 独立构建与全量回归 | `pass` | Qt target built；23/23，0 failed |
| offscreen Qt 纵向闭环 | `pass` | signal/slot → worker → CAN Health → ResultWriter |
| CAN Health | `pass` | 8/8 criteria，heartbeat delta 25，decode/queue/drop 0 |
| Result provenance | `pass` | commit 匹配，`git_dirty=false`，Debug |
| Result evidence boundary | `pass` | `VCAN` / `SIMULATED` |
| SHA-256 清单 | `pass` | `sha256sums.txt` |

`QT_QPA_PLATFORM=offscreen` 验证了 Qt event loop、queued signal、worker、Runtime adapter 和
结果落盘链路；它不等于人工视觉验收窗口布局。

## 不能声称

- 没有 physical CAN、真实 MCU、执行器或电气层证据；
- Qt 与 Runtime 当前同进程，没有 `Qt crash != Runtime crash` 的进程级隔离；
- 没有 Actuator/Jog/Homing、Modbus、EtherCAT、hard realtime 或功能安全证据；
- 软件 `Quick Stop`/Cancel 不能替代硬件 E-stop 或 STO。

## 关键哈希

```text
ctest_qt_off.txt                         205811ab915ad17ed674125fcd2a72f6403d55052accd88f117756bc881814e7
ctest_qt_on.txt                          ca768b5a660411c5cf767448e60ffaf25dd20285206536d372dfc3a0bb1e21c8
qt_offscreen.txt                         942b0d1732eae656ef12ae3b853ebace85aac494de05556a461321f7f08bdd6c
qt-vcan-health-1786443914381.json         c2a009aa96d20982299d20af4411445a447d15813168f96ce8e6e577bb315130
qt-vcan-health-1786443914381.csv          760b72869a632e45ecb5d7110e3d406de855126ba2c9fb753b70afe3c32ede7f
```
