# EtherCAT NIC Gate 证据

本目录存放 ThinkPad 有线网口 EtherCAT **前置条件**探测快照。

- 合同与判定：`docs/ETHERCAT_NIC_GATE.md`（G1–G6：为什么测、不能夸大什么）
- 协议预习：`docs/ETHERCAT_PROTOCOL_NOTES.md`（先读「怎样读 EtherCAT 预习材料」）
- 采集：`sudo ./linux/scripts/collect_ethercat_nic_gate.sh`

每次运行生成 `probe_<UTC>/`，至少含：

| 文件 | 内容 | 主要对应 |
|---|---|---|
| `environment.txt` | 主机、内核、git、是否 root | G6 / 复现元数据 |
| `nic_identity.txt` | PCI、ethtool、ip link | G1 |
| `raw_frame.txt` | AF_PACKET `0x88A4` bind 结果 | G2（≠ 周期） |
| `routing.txt` | 默认路由 / Wi-Fi | G3 |
| `nm_and_packet.txt` | NetworkManager 与 `CONFIG_PACKET` | G4 |
| `soem_slaveinfo.txt` | SOEM 空扫（可无从站） | G5（≠ PDO） |
| `SUMMARY.txt` | 机器可读摘要 | 先读这个 |

`result` 语义与 `docs/EVIDENCE_SCHEMA.md` 一致时：`pass` / `failed` / `permission_denied` / `not_run`。

原始探测文件被 `.gitignore` 忽略；本 README 保留。正式基线应在 `git_dirty=false` 时复跑并在 Gate 文档更新时间戳引用。

## 状态摘要

- `probe_20260804T063253Z`：`nm_managed=no`（unmanaged），raw/SOEM/Wi-Fi 管理面均通过快照检查。
- 开放：干净 commit 复跑（Gate G6）；接上 SubDevice 后的 PDO/OP/WKC 不在本目录范围。

回退 NM 独占：`sudo ./deploy/ethercat/apply_nm_unmanaged.sh --apply --revert`。
