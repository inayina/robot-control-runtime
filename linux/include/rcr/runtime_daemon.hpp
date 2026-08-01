#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// Application composition root：拥有 Runtime、监督器、队列与 I/O 生命周期。

#include "rcr/can_io_loop.hpp"
#include "rcr/owned_fd.hpp"
#include "rcr/result.hpp"
#include "rcr/runtime.hpp"
#include "rcr/runtime_events.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace rcr {

enum class DaemonExitCode : int {
  Ok = 0,
  ConfigError = 1,
  InterfaceError = 2,
  PermissionError = 3,
  WorkerFailure = 4,
};

[[nodiscard]] constexpr std::string_view to_string(DaemonExitCode code) noexcept {
  switch (code) {
    case DaemonExitCode::Ok:
      return "OK";
    case DaemonExitCode::ConfigError:
      return "CONFIG_ERROR";
    case DaemonExitCode::InterfaceError:
      return "INTERFACE_ERROR";
    case DaemonExitCode::PermissionError:
      return "PERMISSION_ERROR";
    case DaemonExitCode::WorkerFailure:
      return "WORKER_FAILURE";
  }
  return "UNKNOWN";
}

struct DaemonConfig {
  std::string can_if{"vcan0"};
  std::uint8_t node_id{1};
  std::chrono::milliseconds period{10};
  std::chrono::milliseconds command_timeout{100};
  std::chrono::milliseconds heartbeat_timeout{300};
  int fifo_priority{0};
  bool require_fifo{false};
  int cpu_affinity{-1};
  std::chrono::milliseconds duration{0};
  std::size_t event_queue_capacity{256};
  std::size_t max_events_per_tick{32};
  std::size_t max_frames_per_wake{32};
  std::size_t trace_capacity{4096};
};

struct DaemonSnapshot {
  RuntimeSnapshot runtime{};
  NodeSupervisorSnapshot node{};
  CanIoStats io{};
  DaemonExitCode exit_code{DaemonExitCode::Ok};
  bool started{false};
  bool stopping{false};
};

/**
 * 可测试的 daemon 服务对象；`rcrd` 的 main 只做参数解析与退出码映射。
 *
 * 启动顺序：signalfd/eventfd → Runtime+supervisor hook → scheduler → I/O。
 * 任一步失败逆序回收，不留下 scheduler/I/O 线程。
 *
 * 备选：全部写在 main()。不选，因为部分启动回滚与同进程集成测试无法稳定验证。
 */
class RuntimeDaemon {
 public:
  explicit RuntimeDaemon(DaemonConfig config);
  ~RuntimeDaemon();

  RuntimeDaemon(const RuntimeDaemon&) = delete;
  RuntimeDaemon& operator=(const RuntimeDaemon&) = delete;

  [[nodiscard]] Result<void> start();
  void request_stop();
  /// 阻塞直到 I/O 停止、duration 到期或 request_stop；随后 join 全部 worker。
  [[nodiscard]] DaemonExitCode wait_and_stop();
  /// 立即请求停止并 join；可重复调用。
  void stop();

  /// Application/测试 API：显式状态迁移与输出发布（生产 CLI 不暴露命令发送）。
  [[nodiscard]] TransitionResult boot();
  [[nodiscard]] TransitionResult activate();
  [[nodiscard]] TransitionResult deactivate();
  [[nodiscard]] TransitionResult clear_fault();
  [[nodiscard]] Result<void> publish_output_command(const OutputCommand& command);

  [[nodiscard]] DaemonSnapshot snapshot() const;
  [[nodiscard]] DaemonExitCode exit_code() const noexcept;
  [[nodiscard]] const DaemonConfig& config() const noexcept { return config_; }
  [[nodiscard]] bool started() const noexcept;

 private:
  void rollback_started_parts();
  void apply_scheduler_affinity();
  [[nodiscard]] DaemonExitCode classify_stop() const;
  void watch_duration();

  DaemonConfig config_;
  EventFd stop_event_{};
  SignalFd signals_{};
  std::unique_ptr<BoundedInputQueue> queue_;
  std::unique_ptr<LinuxRuntime> runtime_;
  std::unique_ptr<NodeSupervisor> supervisor_;
  std::unique_ptr<CanIoLoop> io_;

  mutable std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
  std::atomic<bool> started_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<DaemonExitCode> exit_code_{DaemonExitCode::Ok};
  std::thread duration_thread_;
};

}  // namespace rcr
