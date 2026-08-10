#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// V1 输入边沿/故障事件：不得进入 latest-wins 输出 mailbox。

#include "rcr/can_v1.hpp"
#include "rcr/result.hpp"
#include "rcr/types.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace rcr {

/**
 * I/O → 周期监督的有界事件种类。
 * 故意具体到 CAN V1 单节点，不做通用消息总线。
 */
enum class RuntimeInputKind : std::uint8_t {
  Heartbeat = 0,
  NodeStatus = 1,
  OutputStatus = 2,
  ProtocolReject = 3,
  IoError = 4,
};

[[nodiscard]] constexpr std::string_view to_string(RuntimeInputKind kind) noexcept {
  switch (kind) {
    case RuntimeInputKind::Heartbeat:
      return "HEARTBEAT";
    case RuntimeInputKind::NodeStatus:
      return "NODE_STATUS";
    case RuntimeInputKind::OutputStatus:
      return "OUTPUT_STATUS";
    case RuntimeInputKind::ProtocolReject:
      return "PROTOCOL_REJECT";
    case RuntimeInputKind::IoError:
      return "IO_ERROR";
  }
  return "UNKNOWN";
}

/**
 * 按值传递的输入事件。不跨线程借用栈上的帧缓冲。
 * 接收时刻 monotonic_ns 由 I/O 在 decode 成功后采样，监督超时也基于该时钟域。
 */
struct RuntimeInputEvent {
  RuntimeInputKind kind{RuntimeInputKind::Heartbeat};
  std::int64_t monotonic_ns{0};
  std::uint8_t node_id{0};
  std::uint16_t boot_id{0};
  std::uint16_t session_id{0};
  std::uint16_t hb_seq{0};
  bool interlock_ready{false};
  std::uint16_t input_bits{0};
  std::uint16_t node_fault_code{0};
  can_v1::OutputResult output_result{can_v1::OutputResult::Applied};
  std::uint16_t output_sequence{0};
  std::uint8_t output_mirror{0};
};

/**
 * 固定容量、按值存储的有界队列。
 *
 * 线程模型：I/O 单生产者，周期监督单消费者。用 mutex 而不是无锁环，因为 V1 事件率低，
 * 正确的 overflow 锁存比微优化更重要。
 *
 * 队列满时：拒绝新事件、递增 overflow 计数、置位 overflow_latched；禁止丢掉故障后
 * 假装系统仍正常。溢出本身通过锁存位传达，不靠覆盖队列槽位。
 *
 * 备选：每种消息一个 atomic 最新值。不选，因为节点重启/故障边沿会被后值静默覆盖。
 */
class BoundedInputQueue {
 public:
  explicit BoundedInputQueue(std::size_t capacity);

  BoundedInputQueue(const BoundedInputQueue&) = delete;
  BoundedInputQueue& operator=(const BoundedInputQueue&) = delete;

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::uint64_t push_count() const noexcept;
  [[nodiscard]] std::uint64_t drop_count() const noexcept;
  [[nodiscard]] std::uint64_t overflow_count() const noexcept;
  [[nodiscard]] bool overflow_latched() const noexcept;

  /// 成功入队返回 true；满则锁存 overflow 并返回 false（事件未入队）。
  [[nodiscard]] bool try_push(const RuntimeInputEvent& event);
  [[nodiscard]] std::optional<RuntimeInputEvent> try_pop();

  /// 消费端确认已处理 overflow 后可清除锁存，便于测试；生产路径通常保持锁存到 stop。
  void clear_overflow_latch() noexcept;

 private:
  std::size_t capacity_{1};
  mutable std::mutex mutex_;
  std::vector<RuntimeInputEvent> storage_{};
  std::size_t head_{0};
  std::size_t size_{0};
  std::atomic<std::uint64_t> push_count_{0};
  std::atomic<std::uint64_t> drop_count_{0};
  std::atomic<std::uint64_t> overflow_count_{0};
  std::atomic<bool> overflow_latched_{false};
};

struct NodeSupervisorConfig {
  std::uint8_t node_id{1};
  std::chrono::nanoseconds heartbeat_timeout{std::chrono::milliseconds{300}};
  std::size_t max_events_per_tick{32};
};

struct NodeSupervisorSnapshot {
  bool ever_seen{false};
  bool online{false};
  bool restart_latched{false};
  bool overflow_fault_latched{false};
  bool comm_loss_latched{false};
  std::uint16_t boot_id{0};
  std::uint16_t session_id{0};
  std::uint16_t last_hb_seq{0};
  std::uint16_t node_fault_code{0};
  std::int64_t last_heartbeat_ns{0};
  std::uint64_t heartbeats{0};
  std::uint64_t status_updates{0};
  std::uint64_t protocol_rejects{0};
  std::uint64_t events_processed{0};
  std::uint64_t events_budget_left{0};
};

/**
 * 单节点监督：消费有界队列，维护 online/session，驱动软件状态机故障路径。
 *
 * 不拥有 LinuxRuntime；通过引用回调状态 API。所有 deadline 使用接收端
 * CLOCK_MONOTONIC，不用发送方时间或墙钟。
 */
class NodeSupervisor {
 public:
  explicit NodeSupervisor(NodeSupervisorConfig config, BoundedInputQueue& queue);

  NodeSupervisor(const NodeSupervisor&) = delete;
  NodeSupervisor& operator=(const NodeSupervisor&) = delete;

  /// 在周期线程中调用；now_ns 必须来自 CLOCK_MONOTONIC。
  void on_tick(class LinuxRuntime& runtime, std::int64_t now_ns);

  [[nodiscard]] NodeSupervisorSnapshot snapshot() const;
  [[nodiscard]] const NodeSupervisorConfig& config() const noexcept { return config_; }

  /// 测试/恢复路径：清除重启锁存（不自动 Activate）。
  void clear_restart_latch() noexcept;

  /**
   * 检查所有当前监督 blocker 是否允许显式清 Fault。
   *
   * fault 只是 Runtime 最近一次升级分类，不是 active fault set；因此不能只按它选择一个
   * 条件检查。无论最后分类是什么，队列 overflow、未恢复 CommLoss、节点离线和非零
   * node fault 都会阻止恢复。成功确认 restart latch 仍只回 Idle，不自动 Activate。
   */
  [[nodiscard]] Result<void> acknowledge_fault_clear(FaultCode fault);

 private:
  void apply_event(LinuxRuntime& runtime, const RuntimeInputEvent& event,
                   std::int64_t now_ns);
  void apply_overflow_fault(LinuxRuntime& runtime);
  void apply_comm_loss(LinuxRuntime& runtime, std::int64_t now_ns);
  void note_node_restart(LinuxRuntime& runtime, std::uint16_t boot_id,
                         std::uint16_t session_id);

  NodeSupervisorConfig config_;
  BoundedInputQueue& queue_;
  mutable std::mutex mutex_;
  bool ever_seen_{false};
  bool online_{false};
  bool restart_latched_{false};
  bool overflow_fault_latched_{false};
  bool comm_loss_latched_{false};
  std::uint16_t boot_id_{0};
  std::uint16_t session_id_{0};
  std::uint16_t last_hb_seq_{0};
  std::uint16_t node_fault_code_{0};
  std::int64_t last_heartbeat_ns_{0};
  std::uint64_t heartbeats_{0};
  std::uint64_t status_updates_{0};
  std::uint64_t protocol_rejects_{0};
  std::uint64_t events_processed_{0};
  std::uint64_t events_budget_left_{0};
};

}  // namespace rcr
