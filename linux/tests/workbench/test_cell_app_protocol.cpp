#include "rcr/workbench/application/cell_app_protocol.hpp"

#include "test_support.hpp"

#include <cstdint>
#include <vector>

namespace {

using rcr::workbench::CellAppFrame;
using rcr::workbench::CellAppMessage;
using rcr::workbench::CellAppStatus;
using rcr::workbench::CommandReply;
using rcr::workbench::CommandStatus;
using rcr::workbench::DigitalOutputRequest;
using rcr::workbench::EvidenceClass;
using rcr::workbench::OutputApplyResult;
using rcr::workbench::RuntimeFaultCode;
using rcr::workbench::RuntimeModeCode;
using rcr::workbench::RuntimeTelemetrySnapshot;
using rcr::workbench::kCellAppCrcSize;
using rcr::workbench::kCellAppHeaderSize;
using rcr::workbench::kCellAppOutputPayloadSize;
using rcr::workbench::kCellAppStatusWireSize;

RCR_TEST(status_wire_size_is_exactly_80) {
  CellAppStatus status;
  status.observed_monotonic_ns = 123456789;
  status.mode = RuntimeModeCode::Active;
  status.fault = RuntimeFaultCode::None;
  status.started = true;
  status.interlock_ready = true;
  status.online = true;
  status.position_reached = true;
  status.cell_ready = true;
  status.node_id = 1;
  status.boot_id = 7;
  status.session_id = 9;
  status.last_heartbeat_sequence = 42;
  status.heartbeat_age_ns = 1000;
  status.input_bits = 1;
  status.device_fault_code = 0;
  status.frames_received = 11;
  status.frames_sent = 5;
  status.decode_rejects = 0;
  status.input_queue_drop_count = 2;
  status.last_ack_session = 9;
  status.last_ack_sequence = 3;
  status.last_ack_result = OutputApplyResult::Applied;
  status.ack_pending = false;
  status.evidence = EvidenceClass::Physical;

  std::vector<std::uint8_t> wire;
  RCR_REQUIRE(rcr::workbench::encode_cell_app_status(status, wire));
  RCR_EXPECT(wire.size() == kCellAppStatusWireSize);

  auto decoded = rcr::workbench::decode_cell_app_status(wire);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(decoded.value().observed_monotonic_ns == 123456789);
  RCR_EXPECT(decoded.value().mode == RuntimeModeCode::Active);
  RCR_EXPECT(decoded.value().started);
  RCR_EXPECT(decoded.value().cell_ready);
  RCR_EXPECT(decoded.value().session_id == 9);
  RCR_EXPECT(decoded.value().frames_received == 11);
  RCR_EXPECT(decoded.value().evidence == EvidenceClass::Physical);
}

RCR_TEST(output_payload_keeps_full_digital_output_width) {
  DigitalOutputRequest request;
  request.session_id = 0x100000002ULL;
  request.sequence = 0x200000003ULL;
  request.valid_for_ms = 2000;
  request.mask = 0x00000100U;
  request.values = 0x00000100U;
  const auto payload = rcr::workbench::encode_cell_output_payload(request);
  RCR_EXPECT(payload.size() == kCellAppOutputPayloadSize);
  auto decoded = rcr::workbench::decode_cell_output_payload(payload);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(decoded.value().session_id == request.session_id);
  RCR_EXPECT(decoded.value().sequence == request.sequence);
  RCR_EXPECT(decoded.value().valid_for_ms == 2000);
  RCR_EXPECT(decoded.value().mask == 0x00000100U);
  RCR_EXPECT(decoded.value().values == 0x00000100U);
}

RCR_TEST(frame_round_trip_and_crc) {
  CellAppFrame frame;
  frame.type = CellAppMessage::GetStatus;
  frame.sequence = 17;
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(rcr::workbench::encode_cell_app_frame(frame, bytes));
  RCR_EXPECT(bytes.size() == kCellAppHeaderSize + kCellAppCrcSize);

  std::size_t consumed = 0;
  auto decoded = rcr::workbench::try_decode_cell_app_frame(bytes, consumed);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(consumed == bytes.size());
  RCR_EXPECT(decoded.value().type == CellAppMessage::GetStatus);
  RCR_EXPECT(decoded.value().sequence == 17);

  auto short_buf = bytes;
  short_buf.pop_back();
  std::size_t short_consumed = 99;
  auto incomplete =
      rcr::workbench::try_decode_cell_app_frame(short_buf, short_consumed);
  RCR_EXPECT(!incomplete.ok());
  RCR_EXPECT(incomplete.error().code() == rcr::Errc::WouldBlock);
  RCR_EXPECT(short_consumed == 0);

  bytes[0] ^= 0xFF;
  std::size_t bad_consumed = 0;
  auto bad = rcr::workbench::try_decode_cell_app_frame(bytes, bad_consumed);
  RCR_EXPECT(!bad.ok());
  RCR_EXPECT(bad.error().code() == rcr::Errc::InvalidArgument);
}

RCR_TEST(command_reply_round_trip) {
  CommandReply reply;
  reply.status = CommandStatus::Accepted;
  reply.from_state = RuntimeModeCode::Idle;
  reply.to_state = RuntimeModeCode::Active;
  reply.message = "booted";
  const auto payload = rcr::workbench::encode_cell_command_reply(reply);
  auto decoded = rcr::workbench::decode_cell_command_reply(payload);
  RCR_REQUIRE(decoded.ok());
  RCR_EXPECT(decoded.value().accepted());
  RCR_EXPECT(decoded.value().from_state == RuntimeModeCode::Idle);
  RCR_EXPECT(decoded.value().to_state == RuntimeModeCode::Active);
  RCR_EXPECT(decoded.value().message == "booted");
}

RCR_TEST(status_projection_round_trip) {
  RuntimeTelemetrySnapshot snap;
  snap.observed_monotonic_ns = 55;
  snap.runtime.mode = RuntimeModeCode::Active;
  snap.runtime.started = true;
  snap.device.online = true;
  snap.device.node_id = 1;
  snap.device.session_id = 4;
  snap.position_reached = true;
  snap.cell_ready = true;
  snap.communication.evidence = EvidenceClass::Vcan;
  const auto status = rcr::workbench::project_cell_app_status(snap);
  const auto back = rcr::workbench::cell_status_to_snapshot(status);
  RCR_EXPECT(back.runtime.mode == RuntimeModeCode::Active);
  RCR_EXPECT(back.cell_ready);
  RCR_EXPECT(back.position_reached);
  RCR_EXPECT(back.device.session_id == 4);
  RCR_EXPECT(back.communication.evidence == EvidenceClass::Vcan);
}

} // namespace

RCR_TEST_MAIN()
