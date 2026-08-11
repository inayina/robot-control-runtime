#include "rcr/runtime_daemon.hpp"
#include "rcr/workbench/runtime_application_adapter.hpp"
#include "test_support.hpp"

#include <cstdint>

namespace {

using rcr::DaemonConfig;
using rcr::RuntimeDaemon;
using rcr::workbench::CommandStatus;
using rcr::workbench::CommunicationStopReason;
using rcr::workbench::DigitalOutputRequest;
using rcr::workbench::EvidenceClass;
using rcr::workbench::RuntimeApplicationAdapter;
using rcr::workbench::RuntimeApplicationAdapterConfig;
using rcr::workbench::RuntimeModeCode;

} // namespace

RCR_TEST(ApplicationModelHasStablePresentationStrings) {
  RCR_EXPECT(rcr::workbench::to_string(EvidenceClass::Mock) == "MOCK");
  RCR_EXPECT(rcr::workbench::to_string(EvidenceClass::Vcan) == "VCAN");
  RCR_EXPECT(rcr::workbench::to_string(CommandStatus::Accepted) == "ACCEPTED");
  RCR_EXPECT(rcr::workbench::to_string(RuntimeModeCode::Active) == "ACTIVE");
  RCR_EXPECT(rcr::workbench::to_string(CommunicationStopReason::IoError) ==
             "IO_ERROR");
  RCR_EXPECT(rcr::workbench::to_string(
                 rcr::workbench::DiagnosticSource::Test) == "TEST");
}

RCR_TEST(AdapterProjectsHeadlessSnapshotWithoutStartingTransport) {
  DaemonConfig config{};
  config.can_if = "vcan-test0";
  config.node_id = 7;
  RuntimeDaemon daemon{config};
  RuntimeApplicationAdapter adapter{
      daemon,
      RuntimeApplicationAdapterConfig{EvidenceClass::Vcan, "SOCKETCAN"}};

  const auto snapshot = adapter.snapshot();

  RCR_EXPECT(!snapshot.runtime.started);
  RCR_EXPECT(snapshot.runtime.mode == RuntimeModeCode::Disabled);
  RCR_EXPECT(snapshot.communication.backend == "SOCKETCAN");
  RCR_EXPECT(snapshot.communication.interface_name == "vcan-test0");
  RCR_EXPECT(snapshot.communication.evidence == EvidenceClass::Vcan);
  RCR_EXPECT(snapshot.device.node_id == 7);
  RCR_EXPECT(snapshot.device.device_id == "CAN_NODE_7");
  RCR_EXPECT(snapshot.device.heartbeat_age_ns == -1);
  RCR_EXPECT(!snapshot.diagnostics.empty());
}

RCR_TEST(AdapterDoesNotOwnRuntimeLifecycle) {
  RuntimeDaemon daemon{DaemonConfig{}};
  RuntimeApplicationAdapter adapter{daemon};

  const auto reply = adapter.activate();

  RCR_EXPECT(!reply.accepted());
  RCR_EXPECT(reply.status == CommandStatus::Rejected);
  RCR_EXPECT(reply.from_state == RuntimeModeCode::Disabled);
  RCR_EXPECT(reply.to_state == RuntimeModeCode::Disabled);
}

RCR_TEST(ValidOutputRequestReachesRuntimeAdmission) {
  RuntimeDaemon daemon{DaemonConfig{}};
  RuntimeApplicationAdapter adapter{daemon};
  DigitalOutputRequest request{};
  request.session_id = 1;
  request.sequence = 1;
  request.valid_for_ms = 100;
  request.mask = 1;
  request.values = 1;

  const auto reply = adapter.submit_digital_output(request);

  RCR_EXPECT(!reply.accepted());
  RCR_EXPECT(reply.status == CommandStatus::NotOpen);
}

RCR_TEST(InvalidOutputRequestIsRejectedBeforeRuntime) {
  RuntimeDaemon daemon{DaemonConfig{}};
  RuntimeApplicationAdapter adapter{daemon};
  DigitalOutputRequest request{};
  request.session_id = 1;
  request.sequence = 1;
  request.valid_for_ms = 9;
  request.mask = 0;

  const auto reply = adapter.submit_digital_output(request);

  RCR_EXPECT(!reply.accepted());
  RCR_EXPECT(reply.status == CommandStatus::InvalidArgument);

  request.valid_for_ms = 100;
  const auto empty_mask = adapter.submit_digital_output(request);
  RCR_EXPECT(empty_mask.status == CommandStatus::InvalidArgument);

  request.mask = 1;
  request.values = 256;
  const auto values_too_wide = adapter.submit_digital_output(request);
  RCR_EXPECT(values_too_wide.status == CommandStatus::InvalidArgument);

  request.values = 1;
  request.session_id = 65'536;
  const auto session_too_wide = adapter.submit_digital_output(request);
  RCR_EXPECT(session_too_wide.status == CommandStatus::InvalidArgument);
}

RCR_TEST_MAIN()
