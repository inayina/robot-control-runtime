#pragma once

// RemoteRuntimeClient：进程内 loopback 的请求方。不拥有 socket；通过非拥有的
// RemoteControlEndpoint 做字节往返。真实 TCP/Qt Network 属于后续 worker，不得放进
// MainWindow。

#include "rcr/workbench/application/remote_control_protocol.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rcr::workbench {

enum class RemoteBackendMode : std::uint8_t {
  Local = 0,
  RemoteLoopback = 1,
};

[[nodiscard]] constexpr std::string_view
to_string(RemoteBackendMode mode) noexcept {
  switch (mode) {
  case RemoteBackendMode::Local:
    return "LOCAL";
  case RemoteBackendMode::RemoteLoopback:
    return "REMOTE_LOOPBACK";
  }
  return "UNKNOWN";
}

// Connection 页专用快照：只描述应用边界会话，不替代 Overview 的 Runtime adapter 快照。
struct RemoteConnectionSnapshot {
  RemoteBackendMode mode{RemoteBackendMode::Local};
  bool connected{false};
  std::string evidence_banner{"LOCAL / SAME PROCESS"};
  std::string peer{"n/a"};
  RemoteSessionState session_state{RemoteSessionState::WaitingHello};
  std::uint64_t heartbeats_ok{0};
  std::uint64_t heartbeats_missed{0};
  std::uint64_t status_ok{0};
  std::uint64_t malformed{0};
  std::string last_error{};
  RemoteStatusView last_status{};
};

class RemoteRuntimeClient {
public:
  void set_endpoint(RemoteControlEndpoint *endpoint) noexcept {
    endpoint_ = endpoint;
  }

  [[nodiscard]] bool has_endpoint() const noexcept {
    return endpoint_ != nullptr;
  }

  // 用当前 fixture status 做 HELLO；成功后 session=Established。
  [[nodiscard]] bool connect_session(const RemoteStatusView &status);

  void disconnect_session() noexcept;

  // 已连接时发 HEARTBEAT；对端关闭应答则记 missed，不假装成功。
  [[nodiscard]] bool poll_heartbeat(std::int64_t client_monotonic_ns);

  // 已连接时 GET_STATUS，更新 last_status。
  [[nodiscard]] bool refresh_status();

  [[nodiscard]] RemoteConnectionSnapshot
  snapshot(RemoteBackendMode mode) const;

  [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
  [[nodiscard]] bool exchange(const std::vector<std::uint8_t> &request,
                              RemoteFrame &reply);

  RemoteControlEndpoint *endpoint_{nullptr};
  RemoteFrameParser parser_{};
  bool connected_{false};
  std::uint16_t next_sequence_{1};
  std::uint64_t heartbeats_ok_{0};
  std::uint64_t heartbeats_missed_{0};
  std::uint64_t status_ok_{0};
  std::string last_error_{};
  std::string evidence_tag_{};
  RemoteStatusView last_status_{};
  RemoteSessionState session_state_{RemoteSessionState::WaitingHello};
};

} // namespace rcr::workbench
