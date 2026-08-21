# LD5-05 普通 Linux 调度过载

## Symptom

当周期 callback 被故意延长并同时施加有界 CPU 压力时，应该能观察到周期 deadline miss 和尾部唤醒 lateness。

## Facts

- 环境：普通 Linux x86_64，`SCHED_OTHER`，未请求 FIFO/CPU affinity，`stress-ng` 存在。
- benchmark：周期 `1000 us`、时长 `1000 ms`、`callback_delay_us=3000`；stress-ng 为全部 CPU、3 秒上限。
- benchmark 退出码 `0`，stress-ng 退出码 `0`，`cycles=227`、`sample_count=227`、`deadline_misses=775`。
- `lateness_max_ns=6156167`，`lateness_p99_ns=3040112`；这些是周期唤醒观测值。

## Unknowns

- 本轮把 callback overrun 和 CPU 压力同时打开，不能把两者贡献量单独归因。
- 未在 Orange Pi、PREEMPT_RT 或硬实时控制器上测量。

## Hypotheses

- 3 ms callback 明确跨过 1 ms 周期边界，是 miss 的主要可解释来源；stress-ng 会增加普通 Linux 的调度噪声。

## Experiment

```bash
RCR_BUILD_DIR=build/ld2-qt-off ./linux/scripts/run_ld5_incidents.sh
```

精确 benchmark/stress 命令记录在 `05_scheduler_overload/command.txt`。

## Evidence

- 原始目录：`evidence/ld5_incidents/20260818T135627Z/05_scheduler_overload/`。
- `benchmark_stdout.txt` 保存 counters/percentiles，`samples_lateness_ns.txt` 保存原始样本。
- 场景退出码：`0`；LD5 总场景退出码：`0`。

## Root Cause (only if proved)

对本次注入已证明的原因是 callback 被人为 sleep 3 ms，而计划周期为 1 ms，跨越了多个绝对边界；这不是已证明的 Runtime 代码缺陷。

## Recovery

benchmark join 后退出；stress-ng 在 3 秒上限自然结束，脚本确认退出码后返回。没有留下后台压力进程。

## Fix (or No Code Change)

No Code Change。当前 scheduler 已按跨越边界累计 miss 并跳到未来边界；本次结果用于观察，不触发 Runtime 重构。

## Regression

benchmark 和 stress-ng 均以退出码 `0` 完成，原始样本和汇总已保存；其他四个 LD5 场景同轮通过。

## Residual Risk

这不是 CAN/控制端到端延迟，也不是 PREEMPT_RT、硬实时或功能安全证据；需要分别做不同平台和负载的基线比较。
