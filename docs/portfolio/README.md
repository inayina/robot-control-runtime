# 机器人软件方向作品集（投递入口）

**状态**：Current · 首版对外包  
**日期**：2026-08-05  
**目标岗位**：机器人软件 / 系统软件 / 嵌入式 Linux / ROS2 应用与架构 / 通信中间件 / 边缘 Runtime / 系统集成 / 测试开发  
**主仓**：本仓 `robot-control-runtime`  
**诚实边界**：Not hard realtime · Not functional safety · Not Orange Pi SocketCAN daemon live · 当前公开摘要多为 `git_dirty` experiment/smoke/pilot

---

## 先读这五份

| # | 文件 | 用途 |
|---|---|---|
| 1 | [PERSONAL_NARRATIVE.md](PERSONAL_NARRATIVE.md) | **个人求职叙事**（第一人称故事线：动机 → 转折 → 边界，面试前主线） |
| 2 | [SEVEN_REPOS_OVERVIEW.md](SEVEN_REPOS_OVERVIEW.md) | **七仓统一总纲**（招聘经理 5 分钟 / 技术面 30 分钟总览：一句话主线 + 三组故事 + 证据纪律） |
| 3 | [SYSTEMS_SOFTWARE_PORTFOLIO.md](SYSTEMS_SOFTWARE_PORTFOLIO.md) | **对外主叙事**（本仓项目说明书） |
| 4 | [RESUME_AND_TALK_TRACK.md](RESUME_AND_TALK_TRACK.md) | 简历条、30 秒/2 分钟口述、禁区话术 |
| 5 | [../KNOWLEDGE_BASE.md](../KNOWLEDGE_BASE.md) | 面试深挖：OS / 调度 / epoll / C++ |

## 证据与图

| 资源 | 用途 |
|---|---|
| [../../evidence/portfolio/](../../evidence/portfolio/README.md) | 脱敏摘要索引 |
| [../../evidence/portfolio/figures/](../../evidence/portfolio/figures/README.md) | 拓扑 / 分层 / FIFO / 分段时延四图 |
| [orangepi_rt7_wrapup_20260805.md](../../evidence/portfolio/orangepi_rt7_wrapup_20260805.md) | Real-time Lab 收口与不能声称清单 |
| [PORTFOLIO_V1_RELEASE_PLAN.md](../PORTFOLIO_V1_RELEASE_PLAN.md) | 正式 clean 发布 Gate（尚未全部关闭） |

## 作品集总图

<p align="center">
  <img src="assets/seven_repo_capability_chain.svg" alt="七仓机器人系统软件能力链，实线为已有合同，虚线为共享工程主题" width="100%">
</p>

这张图服务于个人叙事，不把七仓写成已合并部署的产品。MCU 到 Dashboard 的代码/合同数据流见 [digital_twin_end_to_end_dataflow.svg](assets/digital_twin_end_to_end_dataflow.svg)；两图的用途与不能证明的能力见 [assets/README.md](assets/README.md)。

## 姊妹仓怎么引用（相关工程，不是附录）

机器人软件方向作品集以本仓为**主工程**，但臂控制栈与 MCU 链路不是贴上去的彩蛋，而是同一套系统软件直觉在**不同抽象层**的证据：

| 层次 | 仓 | 证明什么 |
|---|---|---|
| 边缘 Linux Runtime | 本仓 | 周期、调度、epoll、SocketCAN、失败语义 |
| 控制 / 总线进程面 | ros2-arm-teleoperation-suite | 多速率、仿真关 FIFO、CANopen/vcan |
| MCU 传感执行面 | robot-state-monitor-v1 | FreeRTOS 任务、串口合同、电机 bench |

写法：**并列相关工程**，标明「未并成单一产品 / 不共享进程」，不要用「附录」二字。  
全部七仓已展开（总表 + 三组故事）见 [SEVEN_REPOS_OVERVIEW.md](SEVEN_REPOS_OVERVIEW.md)；能力边界见 [SISTER_REPOS.md](../SISTER_REPOS.md)。

## 后续仍需实测后再补的图

已补：七仓能力链总图，以及 MCU → Dashboard 的代码/合同数据流图。  
仍待原始证据：电机 bench 曲线与串口抓包。它们应由 twin 仓的台架/日志生成后再引用；当前不以示意图替代实测。
