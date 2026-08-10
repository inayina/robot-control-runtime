#pragma once

// 本文件属于 Orange Pi/Linux Runtime，不是 MCU 共享协议头。
// 节点模拟器的有状态业务逻辑；不拥有 fd，不依赖 LinuxRuntime 状态机。

#include "rcr/can_v1.hpp"
#include "rcr/types.hpp"

#include <cstdint>

namespace rcr {

/**
 * 独立 CAN 节点的会话与输出状态。
 *
 * codec 只保证线级合法；本类负责 session 匹配、序号新鲜度、相对有效期到本地
 * deadline 的判定、已应用普通输出的有界 lease，以及 soft restart 后拒绝旧命令。
 * 可被 Fake 帧单测，也可被 rcr_node_sim 的 epoll 循环驱动。
 *
 * 本类不拥有 SocketCan、timerfd、线程或时钟，只接受调用方采样的 now_ns，因此业务规则
 * 能在无 vcan/无睡眠的单元测试中确定性验证。它不是线程安全对象；rcr_node_sim 的单一
 * epoll 线程串行调用，若未来跨线程使用必须由上层同步。
 */
class CanNodeLogic {
 public:
  struct Config {
    // 构造后 node_id 不变；其余现场量可由 setter/soft_restart 更新。
    std::uint8_t node_id{1};
    std::uint16_t boot_id{1};
    std::uint16_t session_id{1};
    bool interlock_ready{true};
    std::uint16_t input_bits{0};
    std::uint16_t fault_code{0};
  };

  struct HandleResult {
    /// 需要发送 OutputStatus 时为 true（解码失败的帧不发送）。
    bool send_status{false};
    can_v1::WireOutputStatus status{};
    /// 发往本节点命令 ID 但线级非法时递增计数；此时 send_status=false，因为无法保证
    /// session/sequence 等响应字段可信。
    bool protocol_reject{false};
  };

  explicit CanNodeLogic(Config config);

  [[nodiscard]] const Config& config() const noexcept { return config_; }
  [[nodiscard]] std::uint8_t node_id() const noexcept { return config_.node_id; }
  [[nodiscard]] std::uint16_t boot_id() const noexcept { return boot_id_; }
  [[nodiscard]] std::uint16_t session_id() const noexcept { return session_id_; }
  [[nodiscard]] std::uint16_t hb_seq() const noexcept { return hb_seq_; }
  [[nodiscard]] std::uint8_t output_bits() const noexcept { return output_bits_; }
  [[nodiscard]] bool output_lease_active() const noexcept {
    return output_lease_active_;
  }
  [[nodiscard]] std::int64_t output_lease_deadline_ns() const noexcept {
    return output_lease_deadline_ns_;
  }
  [[nodiscard]] std::uint64_t protocol_rejects() const noexcept {
    return protocol_rejects_;
  }
  [[nodiscard]] bool has_accepted_sequence() const noexcept {
    return has_accepted_sequence_;
  }
  [[nodiscard]] std::uint16_t last_accepted_sequence() const noexcept {
    return last_accepted_sequence_;
  }

  void set_interlock_ready(bool ready) noexcept;
  void set_input_bits(std::uint16_t bits) noexcept;
  void set_fault_code(std::uint16_t code) noexcept;

  /// 进程内重启：新 boot/session，清空输出与已接受序号；旧命令不得再生效。
  /// protocol_rejects 保留为进程级诊断累计，不伪装成真正进程重启后持久化的数据。
  void soft_restart();

  /// 构造并返回当前 heartbeat，然后递增内部 hb_seq；允许 u16 自然回绕到 0。
  [[nodiscard]] can_v1::WireHeartbeat make_heartbeat();
  [[nodiscard]] can_v1::WireNodeStatus make_status() const;

  /**
   * 用调用方提供的 CLOCK_MONOTONIC 时间推进普通输出 lease。
   * 到期时输出归零并返回 true；本类不读时钟、不建 timer，真实唤醒由 simulator epoll
   * 线程负责，单元测试可直接传入边界时刻。
   */
  [[nodiscard]] bool expire_output_lease(std::int64_t now_ns) noexcept;

  /**
   * 处理总线上的一帧（接收时刻即判定时刻）。
   * 仅对本节点 OutputCommand CAN ID 做业务处理；其它 ID 忽略。
   */
  [[nodiscard]] HandleResult on_frame(const CanFrame& frame, std::int64_t now_ns);

  /**
   * 对已解码命令做业务判定。
   * receive_ns：帧进入节点的单调时间；now_ns：实际应用判定时间。
   * 延迟响应故障注入应在 due 时刻调用本函数，才能正确触发 EXPIRED。
   * 判定优先级固定为 session → interlock → sequence → expiry → apply，使一次命令只报告
   * 一个确定原因；只有 Applied 才推进 last_accepted_sequence 和 output_bits。
   */
  [[nodiscard]] HandleResult apply_command(const can_v1::WireOutputCommand& cmd,
                                           std::int64_t receive_ns,
                                           std::int64_t now_ns);

 private:
  static std::uint16_t next_nonzero_u16(std::uint16_t value) noexcept;

  // config_ 保存静态 node_id 和当前可上报现场量；boot/session/sequence/output 是会话状态。
  Config config_;
  std::uint16_t boot_id_{1};
  std::uint16_t session_id_{1};
  std::uint16_t hb_seq_{0};
  std::uint8_t output_bits_{0};
  bool output_lease_active_{false};
  std::int64_t output_lease_deadline_ns_{0};
  bool has_accepted_sequence_{false};
  std::uint16_t last_accepted_sequence_{0};
  std::uint64_t protocol_rejects_{0};
};

}  // namespace rcr
