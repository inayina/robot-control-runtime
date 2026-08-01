# ESP32-S3 可选 USB 节点

目标硬件：已有的 ESP32-S3-DevKitC-1-N16R8。状态：V1 不实现。

完成 Orange Pi 的 vcan 端到端与 systemd 部署后，可建立 V1.1 独立实验：通过板载 USB
连接 Orange Pi，学习嵌入式节点 heartbeat、序号、CRC、watchdog、拔插、重启和故障
注入。首版不启用 Wi-Fi、micro-ROS、TWAI 或外接执行器。

约束：

- 默认只上报状态，不生成 Runtime 的控制决策；
- Fault Injection 必须有编译或运行时测试模式，默认关闭；
- 节点重启后使用新 boot/session 标识，旧命令不得重新生效；
- USB 实验使用自己的最小线协议，不提前迫使 SocketCAN 核心抽象为通用 Transport；
- 不连接电机、驱动 Enable 或任何被描述为硬件安全的线路。

计划工具链：ThinkPad 上 ESP-IDF 的 `idf.py build/flash/monitor`。真正开始时再冻结
ESP-IDF 版本、USB 模式和协议，不先生成空工程。
