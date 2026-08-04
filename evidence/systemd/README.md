# evidence/systemd

存放 P3-A1 `systemd-analyze verify` 报告。由 `deploy/systemd/verify_units.sh` 生成。

字段约定：`orange_pi_evidence=false` 表示这是开发机静态检查，不是板上生命周期证据。
正式板上 start/stop/journal 证据属于 P3-B2，不要把本目录报告改写成 PASS 部署。
