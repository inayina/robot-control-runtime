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

struct RuntimeSnapshot {
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
 */
class LinuxRuntime {
 public:
  explicit LinuxRuntime(RuntimeConfig config = {});
  ~LinuxRuntime();

  LinuxRuntime(const LinuxRuntime&) = delete;
  LinuxRuntime& operator=(const LinuxRuntime&) = delete;

  Result<void> start();
  void stop();

  TransitionResult handle(RuntimeEvent event);
  void set_interlock_ready(bool ready);
  Result<void> publish_output_command(const OutputCommand& command);

  [[nodiscard]] std::optional<OutputCommand> try_consume_output_command();
  [[nodiscard]] RuntimeSnapshot snapshot() const;
  [[nodiscard]] std::vector<TraceEvent> trace_snapshot() const;

 private:
  void on_tick(const SchedulerTick& tick);
  void trace_transition(const TransitionResult& transition, std::int64_t now_ns);
  void clear_output_path_locked();

  RuntimeConfig config_;
  mutable std::mutex state_mutex_;
  RuntimeStateMachine state_machine_;
  CommandMailbox mailbox_;
  MonotonicWatchdog command_watchdog_;
  TraceBuffer trace_;
  PeriodicScheduler scheduler_;
  std::optional<std::uint64_t> active_session_id_{};
  std::uint64_t last_output_sequence_{0};
  std::atomic<bool> test_throw_on_tick_{false};
};

}  // namespace rcr
