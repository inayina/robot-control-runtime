# Local Systems Engineering Acceptance Report

状态：**LD8 本机 Release Candidate 验收关闭**  
范围：ThinkPad x86_64、普通 Linux、local/vcan/loopback；不包含 Orange Pi 或物理总线验收。  
验证入口：[`linux/scripts/ci/run_ci_checks.sh`](../linux/scripts/ci/run_ci_checks.sh)

## 1. 验收结论

LD8 只收敛已经实现的本机 Operations、Observability、Diagnostics、Incident、Traceability
和 Thin CI 合同。没有新增 Runtime Core、CAN fd owner、串口 owner、Platform caller 或硬件
能力。最终 clean commit 上重跑 fresh Qt-OFF/Qt-ON 矩阵、非特权测试、文档/脚本静态检查、
临时 prefix Operations 合同和 release manifest/hash；release artifact 的精确 commit 与
SHA256 由运行产生的 `CI_SUMMARY.txt`、`MANIFEST.txt` 和 `.sha256` 保存。

结论保持分层：本报告关闭的是本机 release-candidate Gate，不是 physical CAN、physical
RS-485、Orange Pi 冷启动、Platform 网络容错、功能安全或硬实时 Gate。

## 2. 验收矩阵

| 项 | 实际入口 | LD8 分类 | 边界 |
|---|---|---|---|
| Qt-OFF fresh build + CTest | `run_ci_checks.sh` | PASS / LOCAL | 缺少 vcan 或权限时按测试合同保留 skip |
| Qt-ON fresh build + CTest | `RCR_CI_RUN_QT_ON=1` | PASS / LOCAL | `offscreen` 不证明 GUI 与 Runtime crash isolation |
| Diagnostics fixture/bad-input | `linux/scripts/diagnostics/tests/test_diagnostics.py` | PASS / LOCAL | 只读取既有 trace/summary |
| 文档、JSON、shell、systemd static | `check_docs.py`、`verify_units.sh` | PASS / STATIC | 不启停 host unit |
| Operations install/health/rollback | `deploy/orangepi/test_operations.sh` | PASS / LOCAL LOOPBACK | 临时 prefix、fake systemd、localhost fixture |
| Release artifact/manifest/hash | `install_release.sh` + CI wrapper | PASS / LOCAL | 不等同 Orange Pi 安装 |
| Provisioning draft | `check_provisioning.sh` | `unsupported` 或 `check_pass` | Ansible 缺失不得写成 PASS；不 apply |
| 五类 LD5 incident | `docs/incidents/LD5-*.md` | PASS / LOCAL DIRTY RAW | live systemd、physical CAN/RS-485 仍未执行 |
| Requirements traceability | `docs/REQUIREMENTS_TRACEABILITY_MATRIX.md` | PASS with `REQ-003 PARTIAL` | Platform deferred，不能升级为完整验证 |

最终复核应满足：

```text
git status --porcelain = empty
CI_SUMMARY.git_commit = git rev-parse HEAD
CI_SUMMARY.git_dirty = false
release MANIFEST.git_commit = same HEAD
```

## 3. 可复现命令

Qt-OFF 是必跑矩阵，Qt-ON 是可选矩阵；二者都使用新建构建目录：

```bash
RCR_CI_BUILD_DIR=/tmp/rcr-ld8-qt-off \
RCR_CI_ARTIFACT_DIR=/tmp/rcr-ld8-artifacts-off \
RCR_CI_RUN_QT_ON=0 \
bash linux/scripts/ci/run_ci_checks.sh

RCR_CI_BUILD_DIR=/tmp/rcr-ld8-qt-off-on \
RCR_CI_QT_BUILD_DIR=/tmp/rcr-ld8-qt-on \
RCR_CI_ARTIFACT_DIR=/tmp/rcr-ld8-artifacts-on \
RCR_CI_RUN_QT_ON=1 \
bash linux/scripts/ci/run_ci_checks.sh
```

CI wrapper 不创建 vcan、不启停 host systemd、不打开 `can0` 或 `/dev/ttyS7`。测试缺少
内核能力时只记录 skip/unsupported reason。生成目录可能被系统忽略；提交前后的精确
release identity 以 `CI_SUMMARY.txt`、`MANIFEST.txt` 和 artifact `.sha256` 为准。

## 4. 已关闭与开放缺口

已关闭：

- 本机 source/test acceptance 形成单一 release candidate 入口；
- 六项需求都有合同、实现、测试、incident、raw evidence 和环境状态链路；
- Operations、rollback、health、diagnostics、systemd static 和 provisioning boundary 可复现；
- Runtime Core 没有 LD8 新增 confirmed gap；LD7 的线程观测修复保持为最小测试观察窗口。

仍开放：

- `REQ-003`：Platform/网络失败不改变 Runtime control authority 的真实 Platform caller 验证；
- Orange Pi ARM clean build/install/boot/service lifecycle 和 rollback 的后置 Gate；
- 物理 `can0` heartbeat/status/ACK/lease/CommLoss；
- `/dev/ttyS7` 上真实 RS-485/Modbus RTU agent、断线/恢复/Probe 语义；
- interface-down、live systemd restart、ptrace、network namespace 等曾受权限限制的 incident；
- 普通 Linux 调度观测不升级为 PREEMPT_RT 或硬实时保证。

## 5. Orange Pi entry checklist（仅入口，不是结果）

后置 Gate 开始前重新只读核验：

1. 板上 checkout 与本机 acceptance 相同的 clean commit；
2. aarch64 native build、manifest/hash 与本机 release identity 对齐；
3. 重新记录 kernel flavor、Device Tree、`can0`/MCP2515、`/dev/ttyS7`、占用、服务和 rollback；
4. 明确 root 权限和用户现场动作；
5. 先冻结 wiring/power/termination/站号，再单独授权 physical CAN 或 physical RS-485；
6. 保留 `rcr_cell_app` 为物理 `can0` 唯一 owner，不与 `rcrd` 并行写物理接口。

本报告完成后，Current Gate 停在 LD8；下一步必须由用户选择后置 Orange Pi/physical Gate，
不得自动启动。
