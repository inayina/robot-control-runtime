# 七仓能力边界

审查日期：2026-08-01；本仓 physical CAN 状态补记：2026-08-13。本文用于避免重复建设；
各仓实时状态仍以其自身代码和文档为准。

## 1. 路径

| 角色 | 仓库 | 路径 |
|---|---|---|
| 上游臂栈 | ros2-arm-teleoperation-suite | `/home/ina/dev/ros2-arm-teleoperation-suite` |
| 中游数据 lab | robot-arm-episode-data-lab | `/home/ina/robot-sim-lab/robot-arm-episode-data-lab` |
| 下游 bridge | ros2-moveit-pybullet-bridge | `/home/ina/ros2_ws/src/ros2-moveit-pybullet-bridge` |
| 数字孪生 MCU | robot-state-monitor-v1 | `/home/ina/Documents/PlatformIO/Projects/robot-state-monitor-v1` |
| AMR 仿真 | amr_warehouse_sim | `/home/ina/ros2_ws/src/amr_warehouse_sim` |
| 运维舱 | robot-ops-dashboard | `/home/ina/workspace/robot-ops-dashboard` |
| 本仓 | robot-control-runtime | `/home/ina/dev/robot-control-runtime` |

## 2. 各仓已有重点

| 仓库 | 已覆盖重点 | 本仓不得重复 |
|---|---|---|
| ros2-arm-teleoperation-suite | ROS 2 臂控制、Servo/阻抗、仿真与采集、CANopen/vcan 经验 | 七轴控制器、臂采集栈 |
| robot-arm-episode-data-lab | 数据合同、release、训练、离线评测与 handoff | 数据集与训练流水线 |
| ros2-moveit-pybullet-bridge | PyBullet 回放、风险监控、HOC | Sim2Sim 回放产品与风险 UI |
| robot-state-monitor-v1 | STM32/ESP32、IMU、micro-ROS、TB6612/N20 电机 bench | FreeRTOS/PID/Encoder/PWM、单电机闭环 |
| amr_warehouse_sim | Gazebo、SLAM/Nav2、任务点和 Mock WMS | 导航、地图与 WMS |
| robot-ops-dashboard | FastAPI/WebSocket/MQTT 运维 UI | Dashboard 和控制台前端 |

## 3. 本仓唯一主责

本仓专注六仓尚未形成完整作品证据的部分：

- Orange Pi 上的 ROS-free Linux Runtime；
- POSIX 周期调度、权限降级、epoll、SocketCAN 和 fd 生命周期；
- watchdog、状态机、session/sequence/deadline、trace；
- SSH、systemd、ARM 部署、压力 benchmark 与可复现证据；
- 独立 vcan 节点模拟器和可自动化故障恢复。

这比再搭一套 F411/F103/ESP32 电机与安全链更能补充求职能力，也减少三套工具链、
接线和采购对 Linux 主线的干扰。

## 4. 允许的未来接口

```text
arm stack / AMR sim ── low-rate intent ──► future ROS 2 Adapter
                                               │
                                               ▼
                                      本仓 Runtime API
                                               │
                                        SocketCAN / CAN

本仓 status/trace ── read-only ──► robot-ops-dashboard
```

- ROS 2 Adapter 只转换 Topic 与 Runtime API，不把 ROS executor 带入 Runtime Core。
- Dashboard 只读；高频控制和状态恢复不经过 Web/MQTT。
- `robot-state-monitor-v1` 可提供 MCU 调试经验，但不共享其文本协议或 micro-ROS 主链。
- 三个臂数据仓只在未来通过清晰的 intent/status 合同对接，不共享进程内状态。

## 5. 能力矩阵

| 能力 | 其他仓已有 | 本仓 |
|---|---:|---:|
| 训练、数据与策略评测 | 是 | 不做 |
| 机械臂/AMR 仿真与算法 | 是 | 不做 |
| MCU 电机闭环 | 是 | 不做 |
| 运维 Dashboard | 是 | 不做 UI |
| Linux Runtime 核心 | 零散 | 主责 |
| Orange Pi SSH/systemd 部署 | 未形成统一作品 | 主责 |
| 调度/压力 benchmark | 局部 | 主责 |
| vcan 故障模拟与恢复 | 局部 | 主责 |
| 物理 CAN | 其他仓无本项目证据 | 本仓已有 MCP2515 ↔ STM32 dirty smoke；完整验收仍开放 |

当前不建立七仓超级工程或统一构建系统；文档边界比强行代码复用更合适。
