# evidence/orangepi_can_kernel

Orange Pi 打开 SocketCAN 的构建与安装证据（档1=`vcan`，档2=MCP2515/`can0`）。

档 1 **已关**：can1 可启动，板上已跑过 `vcan0 + rcrd` 软件链。  
档 2 **已板上 probe**：can2 + `mcp2515-can0` overlay 出现 `can0`；不是默认开机，不是
peer 闭环，也不是 B4。权威步骤见
[`docs/ORANGE_PI_CONFIG_CAN_PLAN.md`](../../docs/ORANGE_PI_CONFIG_CAN_PLAN.md)。

| 目录 | 含义 |
|---|---|
| `20260810T120350Z/` | 档1：库存 boot 备份 + `6.6.98-sun60iw2-can1` |
| `20260812T070000Z_can2/` | 档2：MCP2515 / `can0` 构建与 probe 摘要 |
