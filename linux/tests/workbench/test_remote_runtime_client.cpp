#include "rcr/workbench/application/remote_runtime_client.hpp"

#include "test_support.hpp"

#include <cstdint>
#include <string>

namespace {

using rcr::workbench::EvidenceClass;
using rcr::workbench::RemoteBackendMode;
using rcr::workbench::RemoteControlEndpoint;
using rcr::workbench::RemoteRuntimeClient;
using rcr::workbench::RemoteSessionState;
using rcr::workbench::RemoteStatusView;
using rcr::workbench::RuntimeModeCode;

RemoteStatusView sample_status() {
  RemoteStatusView status;
  status.observed_monotonic_ns = 42;
  status.mode = RuntimeModeCode::Idle;
  status.started = true;
  status.online = true;
  status.session_id = 7;
  status.evidence = EvidenceClass::Loopback;
  return status;
}

RCR_TEST(client_hello_heartbeat_status_over_in_process_endpoint) {
  RemoteControlEndpoint endpoint;
  RemoteRuntimeClient client;
  client.set_endpoint(&endpoint);

  RCR_REQUIRE(client.connect_session(sample_status()));
  RCR_EXPECT(client.connected());
  RCR_REQUIRE(client.poll_heartbeat(100));
  RCR_REQUIRE(client.refresh_status());

  const auto snap = client.snapshot(RemoteBackendMode::RemoteLoopback);
  RCR_EXPECT(snap.connected);
  RCR_EXPECT(snap.evidence_banner.find("LOOPBACK") != std::string::npos);
  RCR_EXPECT(snap.session_state == RemoteSessionState::Established);
  RCR_EXPECT(snap.heartbeats_ok == 1);
  RCR_EXPECT(snap.status_ok == 1);
  RCR_EXPECT(snap.last_status.session_id == 7);
  RCR_EXPECT(snap.last_status.mode == RuntimeModeCode::Idle);
}

RCR_TEST(client_misses_heartbeat_when_endpoint_stops_replies) {
  RemoteControlEndpoint endpoint;
  RemoteRuntimeClient client;
  client.set_endpoint(&endpoint);
  RCR_REQUIRE(client.connect_session(sample_status()));
  endpoint.set_heartbeat_replies_enabled(false);
  RCR_EXPECT(!client.poll_heartbeat(200));
  const auto snap = client.snapshot(RemoteBackendMode::RemoteLoopback);
  RCR_EXPECT(snap.heartbeats_missed == 1);
  RCR_EXPECT(snap.heartbeats_ok == 0);
}

RCR_TEST(local_mode_snapshot_does_not_claim_loopback_connected) {
  RemoteControlEndpoint endpoint;
  RemoteRuntimeClient client;
  client.set_endpoint(&endpoint);
  RCR_REQUIRE(client.connect_session(sample_status()));
  const auto local = client.snapshot(RemoteBackendMode::Local);
  RCR_EXPECT(!local.connected);
  RCR_EXPECT(local.evidence_banner.find("LOCAL") != std::string::npos);
  RCR_EXPECT(local.peer == "n/a");
}

} // namespace

RCR_TEST_MAIN()
