#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。

#include "rcr/mailbox.hpp"
#include "rcr/scheduler.hpp"
#include "rcr/state_machine.hpp"
#include "rcr/trace.hpp"
#include "rcr/watchdog.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace rcr {

struct RuntimeConfig {
  SchedulerConfig scheduler{};
  std::chrono::nanoseconds command_timeout{std::chrono::milliseconds{100}};
  std::size_t trace_capacity{4096};
  /**
   * 测试缝：为 true 时下一个 on_tick 抛异常，用于验证 worker 退出后的 fail-closed。
   * 默认 false；正式配置与 daemon 不得启用。异常由 PeriodicScheduler 捕获并停线程。
   */
  bool test_throw_on_tick{false};
};

/// 周期监督钩子：在 watchdog 检查之后、不持有 state_mutex_ 时调用。
/// 钩子内可再调用 handle/set_interlock/set_fault；不得做 socket/磁盘 I/O。
using RuntimeSupervisionHook = std::function<void(std::int64_t now_ns)>;

struct RuntimeSnapshot {
  // snapshot 是跨多个组件拼出的诊断视图，不承诺所有字段属于同一个 CPU 时刻；
  // mode/fault/interlock 在同一 state_mutex 下保持一致，统计字段允许轻微时间差。
  RuntimeMode mode{RuntimeMode::Disabled};
  FaultCode fault{FaultCode::None};
  bool interlock_ready{false};
  bool running{false};
  SchedulerStats scheduler{};
  std::uint64_t published_commands{0};
  std::uint64_t overwritten_commands{0};
  std::uint64_t trace_dropped{0};
};

/**
 * Linux/Orange Pi 上的 Runtime Core 组合根。
 *
 * Application 线程提交状态事件和普通输出命令；唯一周期线程检查命令 watchdog、
 * 记录调度数据并驱动软件状态机。V1 可完全运行在 vcan + 节点模拟器上；本类的
 * 软件联锁和 EStop 只用于学习控制逻辑，不是功能安全实现。
 *
 * 线程与锁：Application/I/O 侧可调用 handle、set_interlock_ready、publish/consume；
 * scheduler worker 调用 on_tick。所有会改变状态机、活动 session、序号、mailbox 门控
 * 和 watchdog 生命周期的操作先取得 state_mutex_，建立统一的状态迁移顺序。
 * TraceBuffer 和 CommandMailbox 各有内部锁；固定锁顺序是 state_mutex_ → 子组件锁，
 * 子组件不会反向调用 LinuxRuntime，因此避免锁顺序环。
 */
class LinuxRuntime {
 public:
  explicit LinuxRuntime(RuntimeConfig config = {});
  ~LinuxRuntime();

  LinuxRuntime(const LinuxRuntime&) = delete;
  LinuxRuntime& operator=(const LinuxRuntime&) = delete;

  /// 启动唯一周期监督线程；不隐式 Boot/Activate，状态机仍由 Application 显式驱动。
  Result<void> start();
  /// 请求并 join worker，随后清空输出路径并把状态机复位到 Disabled；允许重复调用。
  void stop();

  /// 投递离散状态事件。Activate 额外要求 scheduler 正在运行，否则 watchdog 无人监督。
  TransitionResult handle(RuntimeEvent event);
  /// 更新软件联锁信息；Active 中丢失联锁会同步转 Hold 并清空输出路径。
  void set_interlock_ready(bool ready);
  /// 只更新 fault code，不自动迁移；调用方再投递 FaultDetected。
  void set_fault(FaultCode code);
  /// 校验运行状态、会话、序号和绝对 deadline 后发布 latest-wins 普通输出目标。
  Result<void> publish_output_command(const OutputCommand& command);

  /// I/O 消费端取得最新命令；再次检查 scheduler/state/deadline，形成第二道 fail-closed 门。
  [[nodiscard]] std::optional<OutputCommand> try_consume_output_command();
  [[nodiscard]] RuntimeSnapshot snapshot() const;
  [[nodiscard]] std::vector<TraceEvent> trace_snapshot() const;

  /**
   * 注册周期监督钩子（NodeSupervisor 消费有界队列）。
   * 必须在 start() 之前设置；运行中替换不是 V1 合同。传入空函数可清除。
   */
  void set_supervision_hook(RuntimeSupervisionHook hook);

 private:
  void on_tick(const SchedulerTick& tick);
  void trace_transition(const TransitionResult& transition, std::int64_t now_ns);
  void clear_output_path_locked();

  // 构造后配置只读；修改周期、timeout 或 trace 容量需要停止并重建 Runtime。
  RuntimeConfig config_;
  mutable std::mutex state_mutex_;
  RuntimeStateMachine state_machine_;
  CommandMailbox mailbox_;
  MonotonicWatchdog command_watchdog_;
  TraceBuffer trace_;
  PeriodicScheduler scheduler_;
  // active_session_id_ 在一次 Active 周期中由第一条合法命令绑定；离开 Active 就清除。
  // 这两个字段只在 state_mutex_ 下访问，不能单独改为 atomic 后绕过整体校验事务。
  std::optional<std::uint64_t> active_session_id_{};
  std::uint64_t last_output_sequence_{0};
  std::atomic<bool> test_throw_on_tick_{false};
  // 钩子本身在 start 前写入；运行期只读。调用在 state_mutex_ 外，避免与 handle 死锁。
  RuntimeSupervisionHook supervision_hook_{};
};

}  // namespace rcr
