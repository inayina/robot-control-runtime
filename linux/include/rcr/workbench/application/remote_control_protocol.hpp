#pragma once

// Remote 控制面消息语义（v1）：HELLO / HEARTBEAT / GET_STATUS。
// Endpoint 只读写应用层 RemoteStatusView，不持有 RuntimeDaemon 或 Qt。

#include "rcr/workbench/application/application_model.hpp"
#include "rcr/workbench/application/remote_frame.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rcr::workbench {

inline constexpr std::size_t kRemoteStatusWireSize = 64;
inline constexpr std::string_view kRemoteLoopbackEvidenceTag = "LOOPBACK";

// 线上稳定子集：从 RuntimeTelemetrySnapshot 投影，禁止塞入 fd/pointer/私有队列。
struct RemoteStatusView {
  std::int64_t observed_monotonic_ns{-1};
  RuntimeModeCode mode{RuntimeModeCode::Unknown};
  RuntimeFaultCode fault{RuntimeFaultCode::None};
  bool started{false};
  bool online{false};
  std::uint16_t session_id{0};
  std::uint16_t last_heartbeat_sequence{0};
  std::int64_t heartbeat_age_ns{-1};
  std::uint64_t frames_received{0};
  std::uint64_t frames_sent{0};
  std::uint64_t decode_rejects{0};
  std::uint64_t input_queue_drop_count{0};
  EvidenceClass evidence{EvidenceClass::Loopback};
};

[[nodiscard]] RemoteStatusView
project_remote_status(const RuntimeTelemetrySnapshot& snapshot) noexcept;

[[nodiscard]] bool encode_remote_status_payload(const RemoteStatusView& status,
                                                std::vector<std::uint8_t>& out);

[[nodiscard]] bool decode_remote_status_payload(std::span<const std::uint8_t> in,
                                                RemoteStatusView& status);

enum class RemoteSessionState : std::uint8_t {
  WaitingHello = 0,
  Established,
  Faulted,
};

[[nodiscard]] constexpr std::string_view
to_string(RemoteSessionState state) noexcept {
  switch (state) {
  case RemoteSessionState::WaitingHello:
    return "WAITING_HELLO";
  case RemoteSessionState::Established:
    return "ESTABLISHED";
  case RemoteSessionState::Faulted:
    return "FAULTED";
  }
  return "UNKNOWN";
}

struct RemoteEndpointCounters {
  std::uint64_t frames_accepted{0};
  std::uint64_t frames_rejected{0};
  std::uint64_t hellos{0};
  std::uint64_t heartbeats{0};
  std::uint64_t status_replies{0};
  std::uint64_t malformed{0};
};

// 单连接、单线程 endpoint：调用方喂入字节流并取回回复。不创建线程/socket。
class RemoteControlEndpoint {
public:
  void set_status(RemoteStatusView status) noexcept { status_ = status; }
  [[nodiscard]] const RemoteStatusView& status() const noexcept {
    return status_;
  }

  // 故障注入：停止应答 HEARTBEAT，模拟对端假死而不关闭字节流。
  void set_heartbeat_replies_enabled(bool enabled) noexcept {
    heartbeat_replies_enabled_ = enabled;
  }

  void reset_session() noexcept;

  // 将 inbound 追加到解析器；完整请求产生的回复追加到 outbound。
  void push_bytes(std::span<const std::uint8_t> inbound,
                  std::vector<std::uint8_t>& outbound);

  [[nodiscard]] RemoteSessionState session_state() const noexcept {
    return session_state_;
  }
  [[nodiscard]] const RemoteEndpointCounters& counters() const noexcept {
    return counters_;
  }
  [[nodiscard]] std::string_view last_error() const noexcept {
    return last_error_;
  }

private:
  void handle_frame(const RemoteFrame& request,
                    std::vector<std::uint8_t>& outbound);
  void reply_error(std::uint16_t sequence, std::string_view message,
                   std::vector<std::uint8_t>& outbound);

  RemoteStatusView status_{};
  RemoteFrameParser parser_{};
  RemoteSessionState session_state_{RemoteSessionState::WaitingHello};
  RemoteEndpointCounters counters_{};
  bool heartbeat_replies_enabled_{true};
  std::string last_error_{};
};

[[nodiscard]] bool encode_hello(std::uint16_t sequence,
                                std::vector<std::uint8_t>& out);
[[nodiscard]] bool encode_heartbeat(std::uint16_t sequence,
                                    std::int64_t client_monotonic_ns,
                                    std::vector<std::uint8_t>& out);
[[nodiscard]] bool encode_get_status(std::uint16_t sequence,
                                     std::vector<std::uint8_t>& out);

struct RemoteHelloAck {
  std::uint8_t protocol_version{0};
  std::string evidence_tag{};
};

[[nodiscard]] bool decode_hello_ack(std::span<const std::uint8_t> payload,
                                    RemoteHelloAck& ack);

} // namespace rcr::workbench
