#include "rcr/workbench/application/remote_runtime_client.hpp"

namespace rcr::workbench {

bool RemoteRuntimeClient::exchange(const std::vector<std::uint8_t> &request,
                                   RemoteFrame &reply) {
  if (endpoint_ == nullptr) {
    last_error_ = "no endpoint";
    return false;
  }
  std::vector<std::uint8_t> outbound;
  endpoint_->push_bytes(request, outbound);
  if (outbound.empty()) {
    last_error_ = "empty reply";
    return false;
  }
  parser_.clear();
  if (parser_.append(outbound) == RemoteFrameParseStatus::BufferOverflow) {
    last_error_ = "reply buffer overflow";
    return false;
  }
  const auto status = parser_.try_pop(reply);
  if (status != RemoteFrameParseStatus::Ok) {
    last_error_ = std::string(to_string(status));
    return false;
  }
  // 一次请求只期望一帧；多余字节记入 endpoint counters，client 不吞掉不报成功。
  RemoteFrame extra;
  if (parser_.try_pop(extra) == RemoteFrameParseStatus::Ok) {
    last_error_ = "unexpected extra reply frame";
    return false;
  }
  last_error_.clear();
  return true;
}

bool RemoteRuntimeClient::connect_session(const RemoteStatusView &status) {
  if (endpoint_ == nullptr) {
    last_error_ = "no endpoint";
    return false;
  }
  endpoint_->reset_session();
  endpoint_->set_status(status);
  parser_.clear();
  connected_ = false;
  session_state_ = RemoteSessionState::WaitingHello;
  evidence_tag_.clear();
  heartbeats_ok_ = 0;
  heartbeats_missed_ = 0;
  status_ok_ = 0;

  std::vector<std::uint8_t> request;
  if (!encode_hello(next_sequence_++, request)) {
    last_error_ = "encode HELLO failed";
    return false;
  }
  RemoteFrame reply;
  if (!exchange(request, reply)) {
    return false;
  }
  if (reply.type != RemoteMessageType::HelloAck) {
    last_error_ = "expected HELLO_ACK";
    return false;
  }
  RemoteHelloAck ack;
  if (!decode_hello_ack(reply.payload, ack) ||
      ack.protocol_version != kRemoteProtocolVersion) {
    last_error_ = "invalid HELLO_ACK";
    return false;
  }
  evidence_tag_ = ack.evidence_tag;
  connected_ = true;
  session_state_ = RemoteSessionState::Established;
  last_error_.clear();
  return true;
}

void RemoteRuntimeClient::disconnect_session() noexcept {
  connected_ = false;
  session_state_ = RemoteSessionState::WaitingHello;
  evidence_tag_.clear();
  parser_.clear();
  if (endpoint_ != nullptr) {
    endpoint_->reset_session();
  }
}

bool RemoteRuntimeClient::poll_heartbeat(std::int64_t client_monotonic_ns) {
  if (!connected_ || endpoint_ == nullptr) {
    last_error_ = "not connected";
    return false;
  }
  std::vector<std::uint8_t> request;
  if (!encode_heartbeat(next_sequence_++, client_monotonic_ns, request)) {
    last_error_ = "encode HEARTBEAT failed";
    return false;
  }
  std::vector<std::uint8_t> outbound;
  endpoint_->push_bytes(request, outbound);
  if (outbound.empty()) {
    ++heartbeats_missed_;
    last_error_ = "heartbeat reply missing";
    return false;
  }
  parser_.clear();
  static_cast<void>(parser_.append(outbound));
  RemoteFrame reply;
  if (parser_.try_pop(reply) != RemoteFrameParseStatus::Ok ||
      reply.type != RemoteMessageType::HeartbeatAck) {
    ++heartbeats_missed_;
    last_error_ = "heartbeat ack failed";
    return false;
  }
  ++heartbeats_ok_;
  last_error_.clear();
  return true;
}

bool RemoteRuntimeClient::refresh_status() {
  if (!connected_ || endpoint_ == nullptr) {
    last_error_ = "not connected";
    return false;
  }
  std::vector<std::uint8_t> request;
  if (!encode_get_status(next_sequence_++, request)) {
    last_error_ = "encode GET_STATUS failed";
    return false;
  }
  RemoteFrame reply;
  if (!exchange(request, reply) || reply.type != RemoteMessageType::Status) {
    if (last_error_.empty()) {
      last_error_ = "STATUS reply failed";
    }
    return false;
  }
  RemoteStatusView status;
  if (!decode_remote_status_payload(reply.payload, status)) {
    last_error_ = "STATUS decode failed";
    return false;
  }
  last_status_ = status;
  ++status_ok_;
  last_error_.clear();
  return true;
}

RemoteConnectionSnapshot
RemoteRuntimeClient::snapshot(RemoteBackendMode mode) const {
  RemoteConnectionSnapshot view;
  view.mode = mode;
  view.connected = connected_ && mode == RemoteBackendMode::RemoteLoopback;
  if (mode == RemoteBackendMode::Local) {
    view.evidence_banner = "LOCAL / SAME PROCESS";
    view.peer = "n/a";
    view.session_state = RemoteSessionState::WaitingHello;
  } else {
    view.evidence_banner = connected_
                               ? "LOOPBACK / NO PHYSICAL PC-ARM"
                               : "REMOTE_LOOPBACK / DISCONNECTED";
    view.peer = "in-process endpoint";
    view.session_state = session_state_;
  }
  view.heartbeats_ok = heartbeats_ok_;
  view.heartbeats_missed = heartbeats_missed_;
  view.status_ok = status_ok_;
  view.malformed = endpoint_ != nullptr ? endpoint_->counters().malformed : 0;
  view.last_error = last_error_;
  view.last_status = last_status_;
  return view;
}

} // namespace rcr::workbench
