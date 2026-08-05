# 证据 Schema（P2 冻结）

状态：Frozen（P2-W0）  
关联：[P1–P3 详细执行计划](P1_P3_EXECUTION_PLAN.md)

临时终端输出不能跨 commit/机器比较。本文件冻结机器可读字段与结果枚举。
Markdown 只作摘要；原始样本与汇总必须分开存放。

## 1. 结果枚举（不可混用）

| 值 | 含义 |
|---|---|
| `pass` | 命令执行成功且断言满足 |
| `failed` | 命令执行或断言失败（代码/配置问题） |
| `permission_denied` | 缺少权限（如 FIFO / CAN socket），不是代码逻辑 PASS |
| `unsupported` | 环境缺少工具或机制（如 TSan mapping、无 stress-ng） |
| `not_run` | 清单中有、本轮未执行 |

缺失元数据必须报错，不得用猜测值填充。

## 2. 公共环境字段

每个证据目录的 `environment.txt`（或 JSON 同名字段）至少包含：

| 字段 | 说明 |
|---|---|
| `date_utc` | UTC ISO-8601 |
| `hostname` | `hostname` |
| `machine` | `/proc/cpuinfo` 型号摘要或 `uname -m` |
| `cpu_model` | 可读 CPU 名；不可读则 `unavailable` |
| `os_kernel` | `uname -srm` |
| `compiler` | `c++ --version` 首行 |
| `build_type` | Debug/Release/RelWithDebInfo |
| `build_dir` | 相对或绝对构建目录 |
| `git_commit` | `git rev-parse HEAD`；失败则中止 |
| `git_dirty` | `true`/`false`（`git status --porcelain`） |
| `governor` | 各 CPU 的 scaling_governor；不可读 `unavailable` |
| `temp_c` | 可读温度；否则 `unavailable` |
| `stress_ng` | `present`/`missing` |
| `notes` | 可选自由文本 |

正式基线应在 `git_dirty=false` 的干净 commit 上采集；dirty 结果只能当临时对照。

## 3. Benchmark 样本字段

原始：`samples_lateness_ns.txt`（每行一个 `int64` 纳秒，唤醒 lateness）  
汇总：`summary.txt` key=value，至少：

- `result`、`command`、`exit_code`
- `policy_requested`（`other`/`fifo`）、`fifo_priority_requested`
- `fifo_enabled`、`fifo_error`
- `cpu_affinity_requested`（未绑定写 `-1`）、`affinity_enabled`、`affinity_error`
- `load`（`idle`/`stress-ng`）、`stress_command`
- `period_us`、`duration_ms`、`sample_count`
- `lateness_min_ns`、`lateness_mean_ns`、`lateness_max_ns`
- `lateness_p50_ns`、`lateness_p95_ns`、`lateness_p99_ns`、`lateness_p99_9_ns`
- `deadline_misses`、`worker_error`、`cycles`
- `percentile_algorithm`（见仓内 `rcr::percentile_ns` 注释）

空 callback 的 lateness **不是**端到端 CAN/控制延迟。
请求 CPU 编号不等于绑定成功；只有 `affinity_enabled=1` 才能把该格描述为已绑定。

## 4. 目录约定

```text
evidence/
  vcan_acceptance/          # 已有双进程验收
  rcrd_acceptance/          # P1 daemon
  sanitizer/                # asan_ubsan / tsan 报告
  fault_matrix/             # 自动故障矩阵
  thinkpad_baseline/<stamp>/  # 12 组调度矩阵 + environment
  orangepi_baseline/<stamp>/  # 同一 runner，platform=orangepi（含 RT0 pilot）
  orangepi/                   # bring-up 快照与勾选表归档（P3-A2）
  systemd/                    # unit 静态 verify 报告（P3-A1）
  realtime_linux/<run_id>/    # Real-time Lab 正式样本（RT1+）；合同见 REALTIME_EVIDENCE_SCHEMA
  portfolio/                  # 可入库脱敏摘要
```

脚本从**仓库根目录**运行；目标文件已存在则拒绝覆盖（可用显式 `--force` 仅用于本地调试，
正式基线脚本默认无 force）。

Real-time Linux Lab 的工具 I/O、`classification=pilot|baseline|diagnostic`、扩展环境字段
与 `T/D/C/B/J` 任务模型见
[REALTIME_EVIDENCE_SCHEMA.md](REALTIME_EVIDENCE_SCHEMA.md)（RT0 冻结）。P2 本文件继续约束
ThinkPad/Orange Pi 12 格部署对照；二者结果枚举相同，目录与矩阵形状不同，不得混写提升百分比。

## 4.1 Orange Pi 附加环境字段（P3-A2）

在 P2 公共字段之外，板上证据还应区分：

| 字段 | 说明 |
|---|---|
| `platform` | `thinkpad` / `orangepi` |
| `device_tree_model` | `/proc/device-tree/model`；无则 `unavailable` |
| `mem_total_mib` | 从 MemTotal 推导 |
| `systemd_version` | `systemctl --version` 首行 |
| `power_supply_observed` | 人工；模板默认 `NOT_OBSERVED` |
| `boot_media_observed` | 人工 |
| `os_image_source_observed` | 人工 |
| `cpu_big_little_map_observed` | 人工（A76/A55 映射） |
| `undervolt_throttle_observed` | 人工 |
| `service_dropin_archive` | `systemctl cat` 归档路径（B2） |
| `sha256_rcrd_running` | 与 `current/MANIFEST` 对照（B2） |

自动采集入口：`./linux/scripts/collect_orangepi_host_snapshot.sh`。  
勾选表：`deploy/orangepi/BRINGUP_CHECKLIST.md`（未执行=`NOT_RUN`）。

## 5. 入口命令

```bash
# sanitizer
./linux/scripts/run_asan_ubsan.sh
./linux/scripts/run_tsan.sh

# 故障矩阵（缺 vcan0 硬失败）
./linux/scripts/run_fault_matrix.sh

# 12 组矩阵（共享 run_benchmark_matrix.sh；wrapper 只改目录与 platform）
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_thinkpad_benchmark_matrix.sh
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_orangepi_benchmark_matrix.sh

# Orange Pi 主机快照（到货后）
./linux/scripts/collect_orangepi_host_snapshot.sh
```
