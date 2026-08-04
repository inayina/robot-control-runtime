# Orange Pi 4 Pro bring-up 勾选表（P3-A2 模板）

状态：Template（到货前冻结）  
关联：[ORANGE_PI_BRINGUP.md](../../docs/ORANGE_PI_BRINGUP.md)、[EVIDENCE_SCHEMA.md](../../docs/EVIDENCE_SCHEMA.md)

**规则**

- `expected_*` = 采购/合同预期；`observed_*` = 上电后实测；二者分列，禁止混写。
- 未执行项：`result=NOT_RUN`。禁止预填 `PASS`。
- 结果枚举：`PASS` / `FAILED` / `PERMISSION_DENIED` / `UNSUPPORTED` / `NOT_RUN`。
- SSH **只**使用密钥；本文与仓库永不保存密码或私钥。
- ThinkPad 本机练习可填，但 `platform` 必须写清；不得把 x86 结果标成 Orange Pi。

复制本表到 `evidence/orangepi/<stamp>/BRINGUP_FILLED.md` 再填写。

## 0. 元数据

| 字段 | 值 |
|---|---|
| `platform` | `orangepi`（或 `thinkpad_rehearsal`） |
| `git_commit` | |
| `git_dirty` | |
| `operator` | |
| `date_utc` | |
| `checklist_version` | P3-A2 |

---

## B0 硬件与主机基线

| id | 项 | expected | observed | command / 来源 | result | 解释 |
|---|---|---|---|---|---|---|
| B0-01 | 板卡身份 | Orange Pi 4 Pro 4GB | | `cat /proc/device-tree/model`；丝印拍照 | NOT_RUN | |
| B0-02 | SoC 预期 | Allwinner A733（产品页） | | 手册/`lscpu`/DT | NOT_RUN | 产品页≠PASS |
| B0-03 | RAM | 4GB LPDDR5 预期 | | `/proc/meminfo` MemTotal | NOT_RUN | |
| B0-04 | 供电 | 5V/3A Type-C 预期 | | 电源铭牌 + 欠压/降频观察 | NOT_RUN | |
| B0-05 | 启动介质 | （采购时介质） | | 人工记录 eMMC/SD/NVMe | NOT_RUN | |
| B0-06 | OS 镜像来源 | （下载页/hash） | | 人工记录 URL/文件名/校验 | NOT_RUN | |
| B0-07 | 内核 | （镜像自带） | | `uname -srm` | NOT_RUN | |
| B0-08 | 架构 | aarch64 | | `uname -m` | NOT_RUN | |
| B0-09 | 编译器 | （板上安装） | | `c++ --version` | NOT_RUN | |
| B0-10 | 设备树 model | （观察） | | `/proc/device-tree/model` | NOT_RUN | |
| B0-11 | CPU 拓扑 | 大小核，编号待测 | | `lscpu -e`；cpufreq policy | NOT_RUN | 不预设绑核 |
| B0-12 | governor | （默认） | | cpufreq `scaling_governor` | NOT_RUN | 先记录不改 |
| B0-13 | 温度 | 可读则记 | | thermal_zone / 快照脚本 | NOT_RUN | |
| B0-14 | 降频/欠压 | 无异常预期 | | dmesg/日志人工判断 | NOT_RUN | 异常=环境失败 |
| B0-15 | 以太网 | 板载千兆预期 | | `ip -details link`；`ethtool -i` | NOT_RUN | P3 管理网，非 EtherCAT |
| B0-16 | Wi-Fi | Wi-Fi 6 预期 | | `ip link`；接口名 | NOT_RUN | |
| B0-17 | 磁盘空间 | 足够构建 | | `df -h` | NOT_RUN | |
| B0-18 | 时间同步 | NTP/chrony 可用 | | `timedatectl` | NOT_RUN | |
| B0-19 | DNS | 可解析 | | `getent hosts` / `resolvectl` | NOT_RUN | |

自动快照（可选）：

```bash
./linux/scripts/collect_orangepi_host_snapshot.sh
```

---

## SSH（密钥 only）

| id | 项 | expected | observed | command | result | 解释 |
|---|---|---|---|---|---|---|
| SSH-01 | 本机生成密钥 | ed25519/rsa | | `ssh-keygen -t ed25519 -f ~/.ssh/rcr_orangepi -N ''`（口令自定，不入库） | NOT_RUN | 私钥不进 git |
| SSH-02 | 公钥上板 | `authorized_keys` | | `ssh-copy-id -i ~/.ssh/rcr_orangepi.pub user@board` | NOT_RUN | |
| SSH-03 | 密钥登录 | 无密码提示 | | `ssh -i ~/.ssh/rcr_orangepi user@board` | NOT_RUN | |
| SSH-04 | 禁用密码登录（可选加固） | `PasswordAuthentication no` | | 改 `sshd_config` 后重启 sshd | NOT_RUN | 先确认密钥可用 |

禁止：把密码、私钥、`id_rsa` 内容写入本仓或证据 Markdown。

---

## B1 原生构建与功能

| id | 项 | expected | observed | command | result | 解释 |
|---|---|---|---|---|---|---|
| B1-01 | 同 commit | 与 ThinkPad 对照相同 SHA，`git_dirty=false` | | `git rev-parse HEAD`；`git status` | NOT_RUN | |
| B1-02 | configure | 成功 | | `cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Debug` | NOT_RUN | |
| B1-03 | build | 成功 | | `cmake --build build/linux -j` | NOT_RUN | 记录 aarch64 警告 |
| B1-04 | ctest 无 vcan | 非 vcan 测试 PASS | | `ctest --test-dir build/linux --output-on-failure` | NOT_RUN | |
| B1-05 | 创建 vcan | `vcan0` UP | | `sudo ./linux/scripts/setup_vcan.sh vcan0`；`ip -br link show vcan0` | NOT_RUN | |
| B1-06 | SocketCAN/rcrd 验收 | 相关 CTest / acceptance PASS | | `ctest -R 'socketcan|rcrd|runtime_daemon'`；可选 `run_vcan_acceptance.sh` | NOT_RUN | |
| B1-07 | install release | MANIFEST + current | | `sudo ./deploy/orangepi/install_release.sh --apply --activate` | NOT_RUN | |
| B1-08 | binary SHA-256 | 与 MANIFEST 一致 | | `sha256sum` vs MANIFEST | NOT_RUN | |

重启后检查：

```bash
ip -br link show vcan0   # 未装 systemd 前可能 down；B2 后应随 rcr-vcan 恢复
```

---

## B2 systemd / journal / 权限

| id | 项 | expected | observed | command | result | 解释 |
|---|---|---|---|---|---|---|
| B2-01 | 用户 `rcr` | 无登录 shell | | `getent passwd rcr` | NOT_RUN | |
| B2-02 | 安装 unit | 三个 unit 在 `/etc/systemd/system` | | `install` + `daemon-reload` | NOT_RUN | 见 deploy/systemd/README |
| B2-03 | enable vcan+rcrd | active | | `systemctl enable --now rcr-vcan rcrd` | NOT_RUN | |
| B2-04 | sim 默认 | disabled | | `systemctl is-enabled rcr-node-sim` | NOT_RUN | |
| B2-05 | journal | 有 started 日志 | | `journalctl -u rcrd -b` | NOT_RUN | |
| B2-06 | stop 有界 | ≤5s，常态无 SIGKILL | | `time systemctl stop rcrd` | NOT_RUN | |
| B2-07 | 基础无 FIFO | `RestrictRealtime`；无 LimitRTPRIO | | `systemctl show rcrd -p LimitRTPRIO` | NOT_RUN | |
| B2-08 | require-fifo 失败 | 无 drop-in 时失败可见 | | 临时改 ExecStart 测或 drop-in 对照 | NOT_RUN | |
| B2-09 | FIFO drop-in | 成功或记录 OS 具体失败 | | 安装 `rcrd-fifo-affinity.conf.example`（改 CPU） | NOT_RUN | |
| B2-10 | 启动风暴限制 | 30s/3 次 | | 人为搞挂后观察 | NOT_RUN | |
| B2-11 | systemd 版本 | 记录 | | `systemctl --version` | NOT_RUN | |
| B2-12 | service/drop-in 内容 | 归档 | | `systemctl cat rcrd` | NOT_RUN | 贴进证据目录 |
| B2-13 | 运行 binary SHA | 与 current MANIFEST 一致 | | `readlink` + `sha256sum` | NOT_RUN | |

---

## B3 benchmark（共享 runner）

| id | 项 | expected | observed | command | result | 解释 |
|---|---|---|---|---|---|---|
| B3-01 | 时长冻结 | 与 ThinkPad 正式时长相同 | | `RCR_BENCH_DURATION_MS=...` | NOT_RUN | 先 smoke 再冻结 |
| B3-02 | affinity/governor | 记录实际值 | | environment.txt + 人工 | NOT_RUN | |
| B3-03 | 所选 CPU 类型 | A76/A55 注明 | | 拓扑观察 | NOT_RUN | |
| B3-04 | 12 格矩阵 | 每格有 result | | `./linux/scripts/run_orangepi_benchmark_matrix.sh` | NOT_RUN | |
| B3-05 | 对照 ThinkPad | 同 commit/同条件 | | 对比两目录 SUMMARY | NOT_RUN | 不跨条件乱比 |

ThinkPad 入口（同一 runner）：

```bash
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_thinkpad_benchmark_matrix.sh
```

Orange Pi 入口：

```bash
RCR_BENCH_DURATION_MS=5000 ./linux/scripts/run_orangepi_benchmark_matrix.sh
```

---

## B4 恢复 / 断电 / 回滚

| id | 项 | expected | observed | command | result | 解释 |
|---|---|---|---|---|---|---|
| B4-01 | 冷启动 | vcan+rcrd 按设计起来 | | reboot 后 `systemctl is-active` | NOT_RUN | 记录 sim 是否启用 |
| B4-02 | 崩溃重启 | 限次重启，无风暴 | | 杀进程观察 | NOT_RUN | |
| B4-03 | 新 session | 旧命令不重放 | | 按故障矩阵语义 | NOT_RUN | |
| B4-04 | release 回滚 | current 切换成功 | | `rollback_release.sh --apply --restart` | NOT_RUN | 不删旧 release |
| B4-05 | 断电恢复 | 再上电后服务可用 | | 断电实验 | NOT_RUN | |

---

## 明确不能声称

- 本模板填空 ≠ 已完成 P3-B
- `NOT_RUN` 项计入“未测”，不能凑 PASS
- 空 callback lateness ≠ CAN/控制延迟 ≠ 硬实时
