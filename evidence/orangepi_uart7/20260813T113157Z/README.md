# Orange Pi can2 UART7 启用证据

状态：**pass（UART 软件链 / NO PHYSICAL RS-485）**  
日期：2026-08-13  
目标：在保留 MCP2515 `can0` 的同时启用 40-pin pin 8/10 对应的 UART7，并确认实际 tty 与占用。

## 结果

| 检查 | 观察 | 判定 |
|---|---|---|
| 启动档位 | `6.6.98-sun60iw2-can2+`，`kernel_flavor=can2` | pass |
| U-Boot overlay | 依次成功应用 `mcp2515-can0`、`uart7` | pass |
| live DT | `/proc/device-tree/soc@3000000/uart@7080000/status=okay` | pass |
| tty | `/dev/ttyS7`，major/minor `241:7`，`root:dialout`，mode `0660` | pass |
| 驱动 | `/sys/bus/platform/drivers/uart-ng` | pass |
| console/getty | kernel console 只有 `ttyS0`/`tty1`；`serial-getty@ttyS7` disabled/inactive | pass |
| 进程占用 | `fuser -v /dev/ttyS7` 无输出 | pass |
| 串口初始设置 | `9600`、8 data bits、no parity、1 stop bit；这是 tty 当前设置，不是 RTU 收发证据 | observed |
| CAN 回归 | `can0` UP、ERROR-ACTIVE、500 kbit/s、`spi3.0/mcp251x`、错误计数 0 | pass |

启用前已保存板上回滚文件 `/boot/orangepiEnv.txt.pre-uart7`。当前配置为：

```text
kernel_flavor=can2
overlays=mcp2515-can0 uart7
```

板上的 `/home/orangepi/can2-kernel/scripts/dual_boot_can1.sh` 也已备份为
`dual_boot_can1.sh.pre-uart7`，并同步为会在当前配置含 `uart7` 时保留它的版本。同步后脚本
sha256 为 `fc8884ccd8ba1ddbefb6dbb5f6efea2416cdde95dbfca68cc2af7f7e6329e379`；只读
`status` 再次确认 can2、两个 overlay 和当前 `orangepiEnv.txt`。

## 能力边界

本批次只证明 can2 启动环境成功创建且空闲的 `/dev/ttyS7`，并且原 MCP2515 `can0` 未回归。
没有连接 MR0-IOR08，没有发送 RS-485 字节，没有验证 A/B 极性、终端/偏置、RSE、CRC、站号、
寄存器、timeout/recovery 或 Qt physical backend。因此它不是 physical RS-485 或 Modbus RTU PASS。

启动后另观察到 `run-rpc_pipefs.mount` 与 `dnsmasq.service` 为 failed；本批次没有把它们归因于
UART7，也没有将其纳入 UART/RS-485 判定。

## 回滚

若后续发现 pinctrl 或外设冲突：

```bash
sudo cp -a /boot/orangepiEnv.txt.pre-uart7 /boot/orangepiEnv.txt
sudo reboot
```

回滚后仍需复查启动档位和 `can0`，不能只以 SSH 恢复作为成功标准。

## 文件

- [`environment.txt`](environment.txt)：板卡、内核、仓库状态和配置哈希
- [`probe.txt`](probe.txt)：本次验收的关键命令输出
