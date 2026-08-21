# LD5-02 坏发布回滚

## Symptom

新 release 版本不符合 healthcheck 期望时，必须失败并回到明确的 last-known-good release。

## Facts

- 使用临时 `/tmp` prefix、fake systemd 和 CEL1 loopback fixture，不安装 host unit。
- fixture 先安装 release `1111111`，再安装 `2222222`；healthcheck 仍期望 `1111111`，因此失败。
- rollback 目标为已存在的 `1111111`，最终 `current` 指向 `releases/1111111`。

## Unknowns

- 本轮覆盖的是 release version mismatch；真实 bad YAML/config 语义和 live systemd restart 未执行。
- 未验证 Orange Pi 文件系统、电源中断或真实 journal 环境。

## Hypotheses

- healthcheck 的版本匹配门槛能阻止错误 release 被继续当作 healthy，并允许回到 last-known-good。

## Experiment

```bash
RCR_BUILD_DIR=build/ld2-qt-off ./linux/scripts/run_ld5_incidents.sh
```

脚本调用的原子合同测试命令记录在 `02_bad_release_rollback/command.txt`。

## Evidence

- 原始目录：`evidence/ld5_incidents/20260818T135627Z/02_bad_release_rollback/`。
- `stdout.txt` 末行：`operations_contract=pass evidence=LOCAL_LOOPBACK_TEMP_PREFIX`。
- 子测试退出码：`0`；LD5 总场景退出码：`0`。

## Root Cause (only if proved)

本 fixture 中已证明的原因是 active release `2222222` 与期望版本 `1111111` 不匹配；这不是生产配置故障根因。

## Recovery

调用现有 `rollback_release.sh --apply --prefix <temp-prefix> 1111111`，并确认 `current` symlink 回到 `1111111`。

## Fix (or No Code Change)

No Code Change。LD2 已有 install、healthcheck 和 rollback 合同满足本地演练需求。

## Regression

healthy → version mismatch fail → rollback → last-known-good 的路径通过；没有删除 release，也没有吞掉失败。

## Residual Risk

真实 systemd 服务、真实配置解析、Orange Pi 部署和掉电恢复没有证据，不能写成部署 acceptance。
