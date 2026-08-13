# 可选固件实验边界

V1 发布 Gate 不依赖 MCU。用户已单独授权 STM32F103 物理 CAN peer 实验；它使用独立
构建/验收路径，不由 `linux/` CMake 构建，也不反向改变 Orange Pi + vcan 的正式证据。

```text
firmware/
├── esp32s3/     V1.1 可选 USB 诊断/故障注入节点
└── stm32f103/   已实现并做过 dirty physical smoke：bxCAN + CAN V1 + PC13 + SG90 双位置 PWM
```

当前还不是完整 hardware acceptance：PA8/CANH/CANL 波形、断线/bus-off/IWDG、
`rcrd --can can0`、Qt physical Health 和 clean 同提交重烧录均未运行。详细边界见
[`stm32f103/README.md`](stm32f103/README.md)和
[`evidence/stm32f103_can/README.md`](../evidence/stm32f103_can/README.md)。

开始或扩大任何固件前必须先写清楚：它回答哪个 Linux 模拟无法回答的问题、工具链版本、
构建/烧录命令、引脚与电气约束、watchdog/重启行为和验收证据。不能只为了使用手里的
开发板而增加节点。

Orange Pi 只部署 Linux Runtime。固件在 ThinkPad 上使用各自原生工具链构建和烧录，
不建立同时驱动 CMake、ESP-IDF 和 STM32 工具链的超级构建。
