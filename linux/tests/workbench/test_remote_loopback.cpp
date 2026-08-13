#include "rcr/workbench/application/remote_control_protocol.hpp"
#include "rcr/workbench/application/remote_frame.hpp"

#include "test_support.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using rcr::workbench::EvidenceClass;
using rcr::workbench::RemoteControlEndpoint;
using rcr::workbench::RemoteFrame;
using rcr::workbench::RemoteFrameParseStatus;
using rcr::workbench::RemoteFrameParser;
using rcr::workbench::RemoteHelloAck;
using rcr::workbench::RemoteMessageType;
using rcr::workbench::RemoteSessionState;
using rcr::workbench::RemoteStatusView;
using rcr::workbench::RuntimeFaultCode;
using rcr::workbench::RuntimeModeCode;
using rcr::workbench::RuntimeTelemetrySnapshot;
using rcr::workbench::decode_hello_ack;
using rcr::workbench::decode_remote_status_payload;
using rcr::workbench::encode_get_status;
using rcr::workbench::encode_heartbeat;
using rcr::workbench::encode_hello;
using rcr::workbench::encode_remote_status_payload;
using rcr::workbench::kRemoteLoopbackEvidenceTag;
using rcr::workbench::project_remote_status;

[[nodiscard]] bool pop_one(RemoteFrameParser& parser,
                           const std::vector<std::uint8_t>& bytes,
                           RemoteFrame& frame) {
  if (parser.append(bytes) == RemoteFrameParseStatus::BufferOverflow) {
    return false;
  }
  return parser.try_pop(frame) == RemoteFrameParseStatus::Ok;
}

RemoteStatusView sample_status() {
  RemoteStatusView status;
  status.observed_monotonic_ns = 123456789;
  status.mode = RuntimeModeCode::Active;
  status.fault = RuntimeFaultCode::None;
  status.started = true;
  status.online = true;
  status.session_id = 42;
  status.last_heartbeat_sequence = 9;
  status.heartbeat_age_ns = 1000;
  status.frames_received = 10;
  status.frames_sent = 11;
  status.decode_rejects = 1;
  status.input_queue_drop_count = 2;
  status.evidence = EvidenceClass::Loopback;
  return status;
}

RCR_TEST(status_payload_round_trip) {
  const auto original = sample_status();
  std::vector<std::uint8_t> bytes;
  RCR_REQUIRE(encode_remote_status_payload(original, bytes));
  RemoteStatusView decoded;
  RCR_REQUIRE(decode_remote_status_payload(bytes, decoded));
  RCR_EXPECT(decoded.observed_monotonic_ns == original.observed_monotonic_ns);
  RCR_EXPECT(decoded.mode == original.mode);
  RCR_EXPECT(decoded.started == original.started);
  RCR_EXPECT(decoded.online == original.online);
  RCR_EXPECT(decoded.session_id == original.session_id);
  RCR_EXPECT(decoded.frames_received == original.frames_received);
  RCR_EXPECT(decoded.evidence == EvidenceClass::Loopback);
}

RCR_TEST(project_remote_status_copies_application_fields_only) {
  RuntimeTelemetrySnapshot snapshot;
  snapshot.observed_monotonic_ns = 55;
  snapshot.runtime.mode = RuntimeModeCode::Hold;
  snapshot.runtime.started = true;
  snapshot.device.online = false;
  snapshot.device.session_id = 8;
  snapshot.communication.frames_received = 100;
  snapshot.communication.evidence = EvidenceClass::Vcan;
  const auto view = project_remote_status(snapshot);
  RCR_EXPECT(view.observed_monotonic_ns == 55);
  RCR_EXPECT(view.mode == RuntimeModeCode::Hold);
  RCR_EXPECT(view.started);
  RCR_EXPECT(!view.online);
  RCR_EXPECT(view.session_id == 8);
  RCR_EXPECT(view.frames_received == 100);
  RCR_EXPECT(view.evidence == EvidenceClass::Vcan);
}

RCR_TEST(hello_heartbeat_get_status_loopback) {
  RemoteControlEndpoint endpoint;
  endpoint.set_status(sample_status());

  std::vector<std::uint8_t> request;
  std::vector<std::uint8_t> reply;
  RCR_REQUIRE(encode_hello(1, request));
  endpoint.push_bytes(request, reply);
  RCR_EXPECT(endpoint.session_state() == RemoteSessionState::Established);

  RemoteFrameParser parser;
  RemoteFrame hello_ack;
  RCR_REQUIRE(pop_one(parser, reply, hello_ack));
  RCR_EXPECT(hello_ack.type == RemoteMessageType::HelloAck);
  RemoteHelloAck ack;
  RCR_REQUIRE(decode_hello_ack(hello_ack.payload, ack));
  RCR_EXPECT(ack.protocol_version == 1);
  RCR_EXPECT(ack.evidence_tag == kRemoteLoopbackEvidenceTag);

  request.clear();
  reply.clear();
  RCR_REQUIRE(encode_heartbeat(2, 999, request));
  endpoint.push_bytes(request, reply);
  RemoteFrame hb_ack;
  RCR_REQUIRE(pop_one(parser, reply, hb_ack));
  RCR_EXPECT(hb_ack.type == RemoteMessageType::HeartbeatAck);
  RCR_EXPECT(hb_ack.sequence == 2);

  request.clear();
  reply.clear();
  RCR_REQUIRE(encode_get_status(3, request));
  endpoint.push_bytes(request, reply);
  RemoteFrame status_frame;
  RCR_REQUIRE(pop_one(parser, reply, status_frame));
  RCR_EXPECT(status_frame.type == RemoteMessageType::Status);
  RemoteStatusView decoded;
  RCR_REQUIRE(decode_remote_status_payload(status_frame.payload, decoded));
  RCR_EXPECT(decoded.session_id == 42);
  RCR_EXPECT(decoded.evidence == EvidenceClass::Loopback);
  RCR_EXPECT(std::string_view(rcr::workbench::to_string(decoded.evidence)) ==
             "LOOPBACK");
}

RCR_TEST(get_status_before_hello_returns_error) {
  RemoteControlEndpoint endpoint;
  std::vector<std::uint8_t> request;
  std::vector<std::uint8_t> reply;
  RCR_REQUIRE(encode_get_status(9, request));
  endpoint.push_bytes(request, reply);

  RemoteFrameParser parser;
  RemoteFrame frame;
  RCR_REQUIRE(pop_one(parser, reply, frame));
  RCR_EXPECT(frame.type == RemoteMessageType::Error);
  RCR_EXPECT(endpoint.session_state() == RemoteSessionState::WaitingHello);
}

RCR_TEST(malformed_frame_increments_counter_without_false_status) {
  RemoteControlEndpoint endpoint;
  std::vector<std::uint8_t> request;
  std::vector<std::uint8_t> reply;
  RCR_REQUIRE(encode_hello(1, request));
  endpoint.push_bytes(request, reply);
  RCR_EXPECT(endpoint.session_state() == RemoteSessionState::Established);

  reply.clear();
  std::vector<std::uint8_t> bad = request;
  bad.back() ^= 0xFF;
  endpoint.push_bytes(bad, reply);
  RCR_EXPECT(endpoint.counters().malformed >= 1);
  RCR_EXPECT(endpoint.session_state() == RemoteSessionState::Established);
  RCR_EXPECT(endpoint.counters().status_replies == 0);
}

RCR_TEST(stopped_heartbeat_replies_are_observable) {
  RemoteControlEndpoint endpoint;
  endpoint.set_status(sample_status());
  std::vector<std::uint8_t> request;
  std::vector<std::uint8_t> reply;
  RCR_REQUIRE(encode_hello(1, request));
  endpoint.push_bytes(request, reply);

  endpoint.set_heartbeat_replies_enabled(false);
  request.clear();
  reply.clear();
  RCR_REQUIRE(encode_heartbeat(2, 1, request));
  endpoint.push_bytes(request, reply);
  RCR_EXPECT(reply.empty());
  RCR_EXPECT(endpoint.counters().heartbeats == 1);
}

RCR_TEST(partial_request_bytes_do_not_produce_reply_until_complete) {
  RemoteControlEndpoint endpoint;
  std::vector<std::uint8_t> request;
  std::vector<std::uint8_t> reply;
  RCR_REQUIRE(encode_hello(1, request));
  endpoint.push_bytes({request.data(), request.size() / 2}, reply);
  RCR_EXPECT(reply.empty());
  RCR_EXPECT(endpoint.session_state() == RemoteSessionState::WaitingHello);
  endpoint.push_bytes({request.data() + request.size() / 2,
                       request.size() - request.size() / 2},
                      reply);
  RCR_EXPECT(!reply.empty());
  RCR_EXPECT(endpoint.session_state() == RemoteSessionState::Established);
}

} // namespace

RCR_TEST_MAIN()
