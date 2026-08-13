# STM32F103 物理 CAN V1 节点与 SG90 双位置输出

状态：**双向 CAN V1 与 PC13 输出链已运行；SG90 无负载双位置目视动作 PASS；专用仲裁诊断 PASS；waveform not measured；full hardware acceptance PARTIAL**。

目标硬件为 STM32F103C8T6 Blue Pill + 3.3 V SN65HVD230 + ST-Link。它作为 Orange Pi
MCP2515 `can0` 的第二个 active peer，首版只实现 CAN V1 heartbeat/status、命令应答、
普通输出 lease 和 PC13 LED 逻辑输出；扩展使用 PA8/TIM1_CH1，把 bit0 映射成 SG90 的
1.25/1.75 ms 两档 50 Hz PWM。

实现与接线 authority：[`SPEC.md`](SPEC.md)。线级字段仍只以
[`protocol/can_v1/README.md`](../../protocol/can_v1/README.md) 为准。

该板不是“Safety Controller”。普通 Blue Pill、自研固件和 GPIO/LED 行为不能据此宣称
认证急停或功能安全。本实验不重复 FreeRTOS、编码器、PID、连续角度或舵机闭环，也不成为
Orange Pi Runtime V1 发布 Gate 的前置条件。

## 构建

要求：CMake ≥ 3.20、主机 C 编译器、`arm-none-eabi-gcc`/`objcopy`/`size`。当前本地验证使用
GCC 13.3（host）、GNU Arm Embedded 13.2.1、CMake 3.28.3。

主机纯逻辑测试：

```bash
cmake -S firmware/stm32f103 -B build/stm32f103-host \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32f103-host --parallel
ctest --test-dir build/stm32f103-host --output-on-failure
```

ARM 固件：

```bash
cmake -S firmware/stm32f103 -B build/stm32f103-arm \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake
cmake --build build/stm32f103-arm --parallel
```

产物：

```text
build/stm32f103-arm/rcr_stm32f103_can_node.elf
build/stm32f103-arm/rcr_stm32f103_can_node.bin
build/stm32f103-arm/rcr_stm32f103_can_node.hex
build/stm32f103-arm/rcr_stm32f103_can_node.map
```

同一 ARM 构建还生成 `rcr_stm32f103_can_arbitration_probe.{elf,bin}`。它是只用于物理仲裁
取证的一次性诊断固件：关闭 bxCAN 自动重发、以 `0x7FE` 周期尝试发送，并把成功、仲裁失败
和其他发送错误计数留在 SRAM。它不运行 CAN V1 节点、PC13 或 PA8/SG90 逻辑，不能作为正常
固件长期留在板上；测试结束必须恢复并 verify `rcr_stm32f103_can_node.bin`。

应用只使用前 63 KiB Flash；`0x0800FC00..0x0800FFFF` 保留给 boot/session journal。

## 烧录前停止线

1. 核对 [`SPEC.md`](SPEC.md) 的逐线接线、SN65HVD230 `RS` 状态和单一供电来源；
2. 全部断电测 CANH-CANL，两个 120 Ω 并联应接近 60 Ω；
3. 确认 Blue Pill HSE 是 8 MHz；否则当前 500 kbit/s bit timing 不成立；
4. SG90 使用独立稳压 5 V 并与 Blue Pill 共地，禁止从 3.3 V 或 ST-Link 取舵机电源；
5. 首次拆下舵盘或脱离机构；优先在舵机未上电时测量 PA8 波形；
6. 首次只连接 ST-Link，暂时断开 CANH/CANL/GND。

使用 `st-flash` 时，写 BIN 的典型命令是：

```bash
st-flash --reset write \
  build/stm32f103-arm/rcr_stm32f103_can_node.bin 0x08000000
```

也可以在 STM32CubeProgrammer 中写入 HEX/ELF。普通 BIN 写入不应覆盖保留页；`mass erase`
会清除 session journal，重新烧录后必须把 Linux 侧当作新的 commissioning 会话。

## 首轮物理观察

Orange Pi 端先确认接口和错误计数，再观察 MCU 周期帧：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0
candump -t a can0,021:7FF,041:7FF,081:7FF
```

Heartbeat `0x021` payload 的 byte4..5 是当前 session。假设实际观察到 session=`0x000A`，
发送 sequence=1、bit0=1、lease=300 ms：

```bash
cansend can0 061#0101000A0001011E
```

预期 `0x081` 返回 `APPLIED`，PC13 点亮；没有更新命令时约 300 ms 后熄灭。必须用实际
heartbeat session 替换示例 `000A`，不能照抄。随后保存：

```bash
ip -details -statistics link show can0
```

2026-08-13 已完成烧录/verify、周期 heartbeat/status、APPLIED、lease 后 mirror 归零、
STALE_SEQUENCE 和 SESSION_MISMATCH 的真实双向 smoke；详见
[`evidence/stm32f103_can/README.md`](../../evidence/stm32f103_can/README.md)。
约 30 s 连续刷新期间用户目视确认 PC13 点亮，但没有保存照片或电气波形。断线、复位、
bus-off、IWDG 和 `rcrd --can can0` 仍未运行。专用 bxCAN 诊断固件已得到 37 次 `ALST0`、
0 次其他发送错误且 Orange Pi 无错误帧的物理仲裁竞争证据；这不等于 bus-off 或错误计数
阶梯测试，详见同一 evidence 摘要。

SG90 扩展固定使用 `mask=0x01`：`values=0` 是位置 A（1.25 ms），`values=1` 是位置 B
（1.75 ms）。命令源应每 100 ms 使用新 sequence 刷新，停止后 lease 最迟在 deadline 后的
下一个 20 ms PWM 周期关闭控制脉冲。位置 A 的 `output_mirror=0` 与 lease 失效后的 mirror
相同，不能只看 mirror 判断 PA8 是否仍有 PWM。实物验收以 [`SPEC.md`](SPEC.md) 的 S0–S2 为准。
