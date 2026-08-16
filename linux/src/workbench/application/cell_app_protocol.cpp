#include "rcr/workbench/application/cell_app_protocol.hpp"

#include "rcr/workbench/services/modbus_rtu.hpp"

#include <algorithm>

namespace rcr::workbench {
namespace {

void append_u16_le(std::vector<std::uint8_t> &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_u32_le(std::vector<std::uint8_t> &out, std::uint32_t value) {
  append_u16_le(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  append_u16_le(out, static_cast<std::uint16_t>((value >> 16) & 0xFFFFu));
}

void append_u64_le(std::vector<std::uint8_t> &out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
  }
}

void append_i64_le(std::vector<std::uint8_t> &out, std::int64_t value) {
  append_u64_le(out, static_cast<std::uint64_t>(value));
}

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t *p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t *p) noexcept {
  return static_cast<std::uint32_t>(read_u16_le(p)) |
         (static_cast<std::uint32_t>(read_u16_le(p + 2)) << 16);
}

[[nodiscard]] std::uint64_t read_u64_le(const std::uint8_t *p) noexcept {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return value;
}

[[nodiscard]] std::int64_t read_i64_le(const std::uint8_t *p) noexcept {
  return static_cast<std::int64_t>(read_u64_le(p));
}

} // namespace

bool encode_cell_app_frame(const CellAppFrame &frame,
                           std::vector<std::uint8_t> &out) {
  if (frame.payload.size() > kCellAppMaxPayload) {
    return false;
  }
  out.clear();
  append_u32_le(out, kCellAppMagic);
  out.push_back(kCellAppVersion);
  out.push_back(static_cast<std::uint8_t>(frame.type));
  out.push_back(0);
  out.push_back(0);
  append_u16_le(out, frame.sequence);
  append_u16_le(out, static_cast<std::uint16_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  const auto crc = modbus_rtu_crc16(
      std::span<const std::uint8_t>(out.data(), out.size()));
  append_u16_le(out, crc);
  return true;
}

Result<CellAppFrame>
try_decode_cell_app_frame(std::span<const std::uint8_t> bytes,
                          std::size_t &consumed) {
  consumed = 0;
  if (bytes.size() < kCellAppHeaderSize + kCellAppCrcSize) {
    return Error{Errc::WouldBlock, "short cell frame"};
  }
  if (read_u32_le(bytes.data()) != kCellAppMagic) {
    return Error{Errc::InvalidArgument, "bad cell magic"};
  }
  if (bytes[4] != kCellAppVersion) {
    return Error{Errc::InvalidArgument, "bad cell version"};
  }
  const auto payload_size = read_u16_le(bytes.data() + 10);
  if (payload_size > kCellAppMaxPayload) {
    return Error{Errc::InvalidArgument, "cell payload too large"};
  }
  const std::size_t total =
      kCellAppHeaderSize + payload_size + kCellAppCrcSize;
  if (bytes.size() < total) {
    return Error{Errc::WouldBlock, "incomplete cell frame"};
  }
  const auto expect = modbus_rtu_crc16(bytes.subspan(0, total - kCellAppCrcSize));
  const auto got = read_u16_le(bytes.data() + static_cast<std::ptrdiff_t>(total - 2));
  if (expect != got) {
    return Error{Errc::InvalidArgument, "cell crc mismatch"};
  }
  CellAppFrame frame;
  frame.type = static_cast<CellAppMessage>(bytes[5]);
  frame.sequence = read_u16_le(bytes.data() + 8);
  frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kCellAppHeaderSize),
                       bytes.begin() + static_cast<std::ptrdiff_t>(total - 2));
  consumed = total;
  return frame;
}

bool encode_cell_app_status(const CellAppStatus &status,
                            std::vector<std::uint8_t> &out) {
  out.clear();
  out.reserve(kCellAppStatusWireSize);
  append_i64_le(out, status.observed_monotonic_ns);
  out.push_back(static_cast<std::uint8_t>(status.mode));
  out.push_back(static_cast<std::uint8_t>(status.fault));
  out.push_back(status.started ? 1 : 0);
  out.push_back(status.interlock_ready ? 1 : 0);
  out.push_back(status.online ? 1 : 0);
  out.push_back(status.position_reached ? 1 : 0);
  out.push_back(status.cell_ready ? 1 : 0);
  out.push_back(status.node_id);
  append_u16_le(out, status.boot_id);
  append_u16_le(out, status.session_id);
  append_u16_le(out, status.last_heartbeat_sequence);
  append_i64_le(out, status.heartbeat_age_ns);
  append_u16_le(out, status.input_bits);
  append_u16_le(out, status.device_fault_code);
  append_u64_le(out, status.frames_received);
  append_u64_le(out, status.frames_sent);
  append_u64_le(out, status.decode_rejects);
  append_u64_le(out, status.input_queue_drop_count);
  append_u16_le(out, status.last_ack_session);
  append_u16_le(out, status.last_ack_sequence);
  out.push_back(static_cast<std::uint8_t>(status.last_ack_result));
  out.push_back(status.ack_pending ? 1 : 0);
  out.push_back(static_cast<std::uint8_t>(status.evidence));
  // 字段合计 73 字节；补齐到冻结的 80 字节，避免两端各算各的。
  while (out.size() < kCellAppStatusWireSize) {
    out.push_back(0);
  }
  return out.size() == kCellAppStatusWireSize;
}

Result<CellAppStatus>
decode_cell_app_status(std::span<const std::uint8_t> payload) {
  if (payload.size() != kCellAppStatusWireSize) {
    return Error{Errc::InvalidArgument, "cell status size"};
  }
  CellAppStatus status;
  const auto *p = payload.data();
  status.observed_monotonic_ns = read_i64_le(p);
  status.mode = static_cast<RuntimeModeCode>(p[8]);
  status.fault = static_cast<RuntimeFaultCode>(p[9]);
  status.started = p[10] != 0;
  status.interlock_ready = p[11] != 0;
  status.online = p[12] != 0;
  status.position_reached = p[13] != 0;
  status.cell_ready = p[14] != 0;
  status.node_id = p[15];
  status.boot_id = read_u16_le(p + 16);
  status.session_id = read_u16_le(p + 18);
  status.last_heartbeat_sequence = read_u16_le(p + 20);
  status.heartbeat_age_ns = read_i64_le(p + 22);
  status.input_bits = read_u16_le(p + 30);
  status.device_fault_code = read_u16_le(p + 32);
  status.frames_received = read_u64_le(p + 34);
  status.frames_sent = read_u64_le(p + 42);
  status.decode_rejects = read_u64_le(p + 50);
  status.input_queue_drop_count = read_u64_le(p + 58);
  status.last_ack_session = read_u16_le(p + 66);
  status.last_ack_sequence = read_u16_le(p + 68);
  status.last_ack_result = static_cast<OutputApplyResult>(p[70]);
  status.ack_pending = p[71] != 0;
  status.evidence = static_cast<EvidenceClass>(p[72]);
  return status;
}

std::vector<std::uint8_t>
encode_cell_output_payload(const DigitalOutputRequest &request) {
  std::vector<std::uint8_t> out;
  out.reserve(kCellAppOutputPayloadSize);
  append_u64_le(out, request.session_id);
  append_u64_le(out, request.sequence);
  append_u32_le(out, request.valid_for_ms);
  append_u32_le(out, request.mask);
  append_u32_le(out, request.values);
  return out;
}

Result<DigitalOutputRequest>
decode_cell_output_payload(std::span<const std::uint8_t> payload) {
  if (payload.size() != kCellAppOutputPayloadSize) {
    return Error{Errc::InvalidArgument, "cell output payload"};
  }
  DigitalOutputRequest request;
  request.session_id = read_u64_le(payload.data());
  request.sequence = read_u64_le(payload.data() + 8);
  request.valid_for_ms = read_u32_le(payload.data() + 16);
  request.mask = read_u32_le(payload.data() + 20);
  request.values = read_u32_le(payload.data() + 24);
  return request;
}

std::vector<std::uint8_t>
encode_cell_command_reply(const CommandReply &reply) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(reply.status));
  out.push_back(static_cast<std::uint8_t>(reply.from_state));
  out.push_back(static_cast<std::uint8_t>(reply.to_state));
  const auto n = static_cast<std::uint8_t>(
      std::min(reply.message.size(), static_cast<std::size_t>(255)));
  out.push_back(n);
  out.insert(out.end(), reply.message.begin(), reply.message.begin() + n);
  return out;
}

Result<CommandReply>
decode_cell_command_reply(std::span<const std::uint8_t> payload) {
  if (payload.size() < 4) {
    return Error{Errc::InvalidArgument, "short command reply"};
  }
  CommandReply reply;
  reply.status = static_cast<CommandStatus>(payload[0]);
  reply.from_state = static_cast<RuntimeModeCode>(payload[1]);
  reply.to_state = static_cast<RuntimeModeCode>(payload[2]);
  const auto n = payload[3];
  if (payload.size() != static_cast<std::size_t>(4) + n) {
    return Error{Errc::InvalidArgument, "command reply length"};
  }
  reply.message.assign(reinterpret_cast<const char *>(payload.data() + 4), n);
  return reply;
}

CellAppStatus
project_cell_app_status(const RuntimeTelemetrySnapshot &snapshot) noexcept {
  CellAppStatus status;
  status.observed_monotonic_ns = snapshot.observed_monotonic_ns;
  status.mode = snapshot.runtime.mode;
  status.fault = snapshot.runtime.fault;
  status.started = snapshot.runtime.started;
  status.interlock_ready = snapshot.runtime.interlock_ready;
  status.online = snapshot.device.online;
  status.position_reached = snapshot.position_reached;
  status.cell_ready = snapshot.cell_ready;
  status.node_id = snapshot.device.node_id;
  status.boot_id = snapshot.device.boot_id;
  status.session_id = snapshot.device.session_id;
  status.last_heartbeat_sequence = snapshot.device.last_heartbeat_sequence;
  status.heartbeat_age_ns = snapshot.device.heartbeat_age_ns;
  status.input_bits = snapshot.device.input_bits;
  status.device_fault_code = snapshot.device.device_fault_code;
  status.frames_received = snapshot.communication.frames_received;
  status.frames_sent = snapshot.communication.frames_sent;
  status.decode_rejects = snapshot.communication.decode_rejects;
  status.input_queue_drop_count = snapshot.communication.input_queue_drop_count;
  status.last_ack_session = snapshot.output.last_ack_session;
  status.last_ack_sequence = snapshot.output.last_ack_sequence;
  status.last_ack_result = snapshot.output.last_ack_result;
  status.ack_pending = snapshot.output.ack_pending;
  status.evidence = snapshot.communication.evidence;
  return status;
}

RuntimeTelemetrySnapshot
cell_status_to_snapshot(const CellAppStatus &status) {
  RuntimeTelemetrySnapshot snap;
  snap.observed_monotonic_ns = status.observed_monotonic_ns;
  snap.runtime.mode = status.mode;
  snap.runtime.fault = status.fault;
  snap.runtime.started = status.started;
  snap.runtime.interlock_ready = status.interlock_ready;
  snap.runtime.scheduler_running = status.started;
  snap.device.online = status.online;
  snap.device.node_id = status.node_id;
  snap.device.device_id = "CAN_NODE_" + std::to_string(status.node_id);
  snap.device.boot_id = status.boot_id;
  snap.device.session_id = status.session_id;
  snap.device.last_heartbeat_sequence = status.last_heartbeat_sequence;
  snap.device.heartbeat_age_ns = status.heartbeat_age_ns;
  snap.device.input_bits = status.input_bits;
  snap.device.device_fault_code = status.device_fault_code;
  snap.device.ever_seen = status.online || status.session_id != 0;
  snap.communication.backend = "SOCKETCAN";
  snap.communication.evidence = status.evidence;
  snap.communication.frames_received = status.frames_received;
  snap.communication.frames_sent = status.frames_sent;
  snap.communication.decode_rejects = status.decode_rejects;
  snap.communication.input_queue_drop_count = status.input_queue_drop_count;
  snap.output.last_ack_session = status.last_ack_session;
  snap.output.last_ack_sequence = status.last_ack_sequence;
  snap.output.last_ack_result = status.last_ack_result;
  snap.output.ack_pending = status.ack_pending;
  snap.position_reached = status.position_reached;
  snap.cell_ready = status.cell_ready;
  return snap;
}

} // namespace rcr::workbench
