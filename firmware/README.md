# 可选固件实验边界

V1 没有 MCU 固件工程。该目录只记录已有开发板未来可能承担的独立学习实验，不由
`linux/` CMake 构建，也不影响 Orange Pi + vcan 的验收。

```text
firmware/
├── esp32s3/     V1.1 可选 USB 诊断/故障注入节点
└── stm32f103/   暂停；以后按明确裸机或物理 CAN 问题立项
```

开始任何固件前必须先写清楚：它回答哪个 Linux 模拟无法回答的问题、工具链版本、
构建/烧录命令、引脚与电气约束、watchdog/重启行为和验收证据。不能只为了使用手里的
开发板而增加节点。

Orange Pi 只部署 Linux Runtime。固件在 ThinkPad 上使用各自原生工具链构建和烧录，
不建立同时驱动 CMake、ESP-IDF 和 STM32 工具链的超级构建。
