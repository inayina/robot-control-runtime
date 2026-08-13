#include "rcr/workbench/application/remote_control_protocol.hpp"

#include <cstring>

namespace rcr::workbench {
namespace {

void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
  }
}

void append_i64_le(std::vector<std::uint8_t>& out, std::int64_t value) {
  append_u64_le(out, static_cast<std::uint64_t>(value));
}

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint64_t read_u64_le(const std::uint8_t* p) noexcept {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return value;
}

[[nodiscard]] std::int64_t read_i64_le(const std::uint8_t* p) noexcept {
  return static_cast<std::int64_t>(read_u64_le(p));
}

[[nodiscard]] bool encode_frame(RemoteMessageType type, std::uint16_t sequence,
                                std::vector<std::uint8_t> payload,
                                std::vector<std::uint8_t>& out) {
  RemoteFrame frame;
  frame.type = type;
  frame.sequence = sequence;
  frame.payload = std::move(payload);
  return encode_remote_frame(frame, out);
}

} // namespace

RemoteStatusView
project_remote_status(const RuntimeTelemetrySnapshot& snapshot) noexcept {
  RemoteStatusView view;
  view.observed_monotonic_ns = snapshot.observed_monotonic_ns;
  view.mode = snapshot.runtime.mode;
  view.fault = snapshot.runtime.fault;
  view.started = snapshot.runtime.started;
  view.online = snapshot.device.online;
  view.session_id = snapshot.device.session_id;
  view.last_heartbeat_sequence = snapshot.device.last_heartbeat_sequence;
  view.heartbeat_age_ns = snapshot.device.heartbeat_age_ns;
  view.frames_received = snapshot.communication.frames_received;
  view.frames_sent = snapshot.communication.frames_sent;
  view.decode_rejects = snapshot.communication.decode_rejects;
  view.input_queue_drop_count = snapshot.communication.input_queue_drop_count;
  view.evidence = snapshot.communication.evidence;
  if (view.evidence == EvidenceClass::Unspecified) {
    view.evidence = EvidenceClass::Loopback;
  }
  return view;
}

bool encode_remote_status_payload(const RemoteStatusView& status,
                                  std::vector<std::uint8_t>& out) {
  // 固定 64 字节：字段增减必须升 version，禁止“变长 JSON 偷懒”。
  out.clear();
  out.reserve(kRemoteStatusWireSize);
  append_i64_le(out, status.observed_monotonic_ns);
  out.push_back(static_cast<std::uint8_t>(status.mode));
  out.push_back(static_cast<std::uint8_t>(status.fault));
  out.push_back(status.started ? 1 : 0);
  out.push_back(status.online ? 1 : 0);
  append_u16_le(out, status.session_id);
  append_u16_le(out, status.last_heartbeat_sequence);
  append_i64_le(out, status.heartbeat_age_ns);
  append_u64_le(out, status.frames_received);
  append_u64_le(out, status.frames_sent);
  append_u64_le(out, status.decode_rejects);
  append_u64_le(out, status.input_queue_drop_count);
  out.push_back(static_cast<std::uint8_t>(status.evidence));
  while (out.size() < kRemoteStatusWireSize) {
    out.push_back(0);
  }
  return out.size() == kRemoteStatusWireSize;
}

bool decode_remote_status_payload(std::span<const std::uint8_t> in,
                                  RemoteStatusView& status) {
  if (in.size() != kRemoteStatusWireSize) {
    return false;
  }
  const std::uint8_t* p = in.data();
  status.observed_monotonic_ns = read_i64_le(p);
  status.mode = static_cast<RuntimeModeCode>(p[8]);
  status.fault = static_cast<RuntimeFaultCode>(p[9]);
  status.started = p[10] != 0;
  status.online = p[11] != 0;
  status.session_id = read_u16_le(p + 12);
  status.last_heartbeat_sequence = read_u16_le(p + 14);
  status.heartbeat_age_ns = read_i64_le(p + 16);
  status.frames_received = read_u64_le(p + 24);
  status.frames_sent = read_u64_le(p + 32);
  status.decode_rejects = read_u64_le(p + 40);
  status.input_queue_drop_count = read_u64_le(p + 48);
  status.evidence = static_cast<EvidenceClass>(p[56]);
  return true;
}

void RemoteControlEndpoint::reset_session() noexcept {
  parser_.clear();
  session_state_ = RemoteSessionState::WaitingHello;
  counters_ = {};
  heartbeat_replies_enabled_ = true;
  last_error_.clear();
}

void RemoteControlEndpoint::push_bytes(std::span<const std::uint8_t> inbound,
                                       std::vector<std::uint8_t>& outbound) {
  if (parser_.append(inbound) == RemoteFrameParseStatus::BufferOverflow) {
    ++counters_.malformed;
    last_error_ = "rx buffer overflow";
    session_state_ = RemoteSessionState::Faulted;
    reply_error(0, last_error_, outbound);
    return;
  }

  for (;;) {
    RemoteFrame frame;
    const auto status = parser_.try_pop(frame);
    if (status == RemoteFrameParseStatus::NeedMore) {
      return;
    }
    if (status != RemoteFrameParseStatus::Ok) {
      ++counters_.malformed;
      ++counters_.frames_rejected;
      last_error_ = std::string(to_string(status));
      // 坏帧不立刻拆掉已 HELLO 的会话，允许对端继续；连续故障由上层观察 counters。
      continue;
    }
    handle_frame(frame, outbound);
  }
}

void RemoteControlEndpoint::handle_frame(
    const RemoteFrame& request, std::vector<std::uint8_t>& outbound) {
  ++counters_.frames_accepted;

  switch (request.type) {
  case RemoteMessageType::Hello: {
    ++counters_.hellos;
    std::vector<std::uint8_t> payload;
    payload.push_back(kRemoteProtocolVersion);
    const auto tag = kRemoteLoopbackEvidenceTag;
    payload.insert(payload.end(), tag.begin(), tag.end());
    payload.push_back(0);
    if (!encode_frame(RemoteMessageType::HelloAck, request.sequence,
                      std::move(payload), outbound)) {
      last_error_ = "encode HELLO_ACK failed";
      return;
    }
    session_state_ = RemoteSessionState::Established;
    last_error_.clear();
    return;
  }
  case RemoteMessageType::Heartbeat: {
    if (session_state_ != RemoteSessionState::Established) {
      reply_error(request.sequence, "HEARTBEAT before HELLO", outbound);
      return;
    }
    ++counters_.heartbeats;
    if (!heartbeat_replies_enabled_) {
      // 故意不应答：上层用 timeout 观察，而不是静默把会话标成成功。
      return;
    }
    std::vector<std::uint8_t> payload;
    if (request.payload.size() == 8) {
      payload.assign(request.payload.begin(), request.payload.end());
    } else {
      append_i64_le(payload, status_.observed_monotonic_ns);
    }
    if (!encode_frame(RemoteMessageType::HeartbeatAck, request.sequence,
                      std::move(payload), outbound)) {
      last_error_ = "encode HEARTBEAT_ACK failed";
    }
    return;
  }
  case RemoteMessageType::GetStatus: {
    if (session_state_ != RemoteSessionState::Established) {
      reply_error(request.sequence, "GET_STATUS before HELLO", outbound);
      return;
    }
    std::vector<std::uint8_t> payload;
    if (!encode_remote_status_payload(status_, payload)) {
      reply_error(request.sequence, "status encode failed", outbound);
      return;
    }
    if (!encode_frame(RemoteMessageType::Status, request.sequence,
                      std::move(payload), outbound)) {
      reply_error(request.sequence, "STATUS encode failed", outbound);
      return;
    }
    ++counters_.status_replies;
    return;
  }
  default:
    ++counters_.frames_rejected;
    reply_error(request.sequence, "unsupported message type", outbound);
    return;
  }
}

void RemoteControlEndpoint::reply_error(std::uint16_t sequence,
                                        std::string_view message,
                                        std::vector<std::uint8_t>& outbound) {
  last_error_ = std::string(message);
  std::vector<std::uint8_t> payload;
  payload.push_back(1); // code：通用应用错误；细分码留给后续 Gate
  payload.push_back(0);
  const auto n =
      message.size() > 64 ? std::size_t{64} : message.size();
  payload.insert(payload.end(), message.begin(), message.begin() +
                                                     static_cast<std::ptrdiff_t>(n));
  if (!encode_frame(RemoteMessageType::Error, sequence, std::move(payload),
                    outbound)) {
    // 错误帧本身编码失败时只能保留 last_error_；避免递归再发 Error。
  }
}

bool encode_hello(std::uint16_t sequence, std::vector<std::uint8_t>& out) {
  std::vector<std::uint8_t> payload{kRemoteProtocolVersion};
  return encode_frame(RemoteMessageType::Hello, sequence, std::move(payload),
                      out);
}

bool encode_heartbeat(std::uint16_t sequence, std::int64_t client_monotonic_ns,
                      std::vector<std::uint8_t>& out) {
  std::vector<std::uint8_t> payload;
  append_i64_le(payload, client_monotonic_ns);
  return encode_frame(RemoteMessageType::Heartbeat, sequence, std::move(payload),
                      out);
}

bool encode_get_status(std::uint16_t sequence, std::vector<std::uint8_t>& out) {
  return encode_frame(RemoteMessageType::GetStatus, sequence, {}, out);
}

bool decode_hello_ack(std::span<const std::uint8_t> payload,
                      RemoteHelloAck& ack) {
  if (payload.empty()) {
    return false;
  }
  ack.protocol_version = payload[0];
  if (payload.size() == 1) {
    ack.evidence_tag.clear();
    return true;
  }
  const auto* begin = reinterpret_cast<const char*>(payload.data() + 1);
  const auto max_len = payload.size() - 1;
  const auto len = ::strnlen(begin, max_len);
  ack.evidence_tag.assign(begin, len);
  return true;
}

} // namespace rcr::workbench
