#include "rcr/workbench/profile/mock_modbus_io_profile.hpp"

#include "test_support.hpp"

#include <cstddef>

namespace {

using rcr::workbench::MockModbusIoProfile;
using rcr::workbench::ModbusDeviceState;
using rcr::workbench::ModbusIoCommandStatus;
using rcr::workbench::ModbusScanState;

RCR_TEST(initial_state_is_explicit_mock) {
  MockModbusIoProfile profile;
  const auto snapshot = profile.snapshot();
  RCR_EXPECT(snapshot.backend == "MOCK");
  RCR_EXPECT(snapshot.no_physical_rs485);
  RCR_EXPECT(snapshot.serial_port == "NOT CONNECTED");
  RCR_EXPECT(snapshot.scan_state == ModbusScanState::Unknown);
  RCR_EXPECT(snapshot.device_state == ModbusDeviceState::Online);
}

RCR_TEST(scan_reports_online_and_timeout_slaves) {
  MockModbusIoProfile profile;
  RCR_REQUIRE(profile.begin_scan(10));
  RCR_EXPECT(profile.snapshot().scan_state == ModbusScanState::Scanning);
  RCR_REQUIRE(profile.complete_scan(20));
  const auto snapshot = profile.snapshot();
  RCR_EXPECT(snapshot.scan_state == ModbusScanState::Complete);
  RCR_REQUIRE(snapshot.slaves.size() == 3);
  RCR_EXPECT(snapshot.slaves[0].state == ModbusDeviceState::Online);
  RCR_EXPECT(snapshot.slaves[1].state == ModbusDeviceState::Timeout);
  RCR_EXPECT(snapshot.slaves[2].state == ModbusDeviceState::Online);
}

RCR_TEST(repeated_scan_recovers_device_state) {
  MockModbusIoProfile profile;
  profile.set_next_write_outcome(ModbusIoCommandStatus::Timeout);
  const auto timeout = profile.write_digital_output(0, true, 10);
  RCR_EXPECT(timeout.status == ModbusIoCommandStatus::Timeout);
  RCR_EXPECT(profile.snapshot().device_state == ModbusDeviceState::Timeout);
  RCR_REQUIRE(profile.begin_scan(20));
  RCR_REQUIRE(profile.complete_scan(30));
  RCR_EXPECT(profile.snapshot().device_state == ModbusDeviceState::Online);
}

RCR_TEST(all_four_digital_inputs_are_independent) {
  MockModbusIoProfile profile;
  for (std::size_t channel = 0; channel < 4; ++channel) {
    const auto reply = profile.set_mock_digital_input(
        channel, (channel % 2U) == 0U, static_cast<std::int64_t>(channel + 1));
    RCR_EXPECT(reply.accepted());
  }
  const auto snapshot = profile.snapshot();
  RCR_EXPECT(snapshot.digital_inputs[0].active);
  RCR_EXPECT(!snapshot.digital_inputs[1].active);
  RCR_EXPECT(snapshot.digital_inputs[2].active);
  RCR_EXPECT(!snapshot.digital_inputs[3].active);
}

RCR_TEST(invalid_input_channel_is_rejected) {
  MockModbusIoProfile profile;
  const auto reply = profile.set_mock_digital_input(4, true, 1);
  RCR_EXPECT(reply.status == ModbusIoCommandStatus::InvalidChannel);
  for (const auto &input : profile.snapshot().digital_inputs) {
    RCR_EXPECT(!input.active);
  }
}

RCR_TEST(output_success_updates_requested_and_confirmed) {
  MockModbusIoProfile profile;
  auto reply = profile.write_digital_output(0, true, 1);
  RCR_EXPECT(reply.accepted());
  RCR_EXPECT(profile.snapshot().digital_outputs[0].requested);
  RCR_EXPECT(profile.snapshot().digital_outputs[0].confirmed);
  reply = profile.write_digital_output(0, false, 2);
  RCR_EXPECT(reply.accepted());
  RCR_EXPECT(!profile.snapshot().digital_outputs[0].requested);
  RCR_EXPECT(!profile.snapshot().digital_outputs[0].confirmed);
}

RCR_TEST(failed_writes_never_change_confirmed_state) {
  MockModbusIoProfile profile;
  RCR_REQUIRE(profile.write_digital_output(0, true, 1).accepted());
  const ModbusIoCommandStatus failures[]{ModbusIoCommandStatus::Timeout,
                                         ModbusIoCommandStatus::Exception,
                                         ModbusIoCommandStatus::Rejected};
  std::int64_t now = 2;
  for (const auto failure : failures) {
    profile.set_next_write_outcome(failure);
    const auto reply = profile.write_digital_output(0, false, now++);
    RCR_EXPECT(reply.status == failure);
    RCR_EXPECT(!profile.snapshot().digital_outputs[0].requested);
    RCR_EXPECT(profile.snapshot().digital_outputs[0].confirmed);
  }
}

RCR_TEST(all_off_is_confirmed_as_one_mock_use_case) {
  MockModbusIoProfile profile;
  for (std::size_t channel = 0; channel < 4; ++channel) {
    RCR_REQUIRE(profile
                    .write_digital_output(
                        channel, true, static_cast<std::int64_t>(channel + 1))
                    .accepted());
  }
  RCR_REQUIRE(profile.write_all_outputs_off(10).accepted());
  for (const auto &output : profile.snapshot().digital_outputs) {
    RCR_EXPECT(!output.requested);
    RCR_EXPECT(!output.confirmed);
  }
}

RCR_TEST(failed_all_off_keeps_confirmed_outputs) {
  MockModbusIoProfile profile;
  RCR_REQUIRE(profile.write_digital_output(1, true, 1).accepted());
  profile.set_next_write_outcome(ModbusIoCommandStatus::Timeout);
  const auto reply = profile.write_all_outputs_off(2);
  RCR_EXPECT(reply.status == ModbusIoCommandStatus::Timeout);
  RCR_EXPECT(!profile.snapshot().digital_outputs[1].requested);
  RCR_EXPECT(profile.snapshot().digital_outputs[1].confirmed);
}

RCR_TEST(non_monotonic_time_is_rejected) {
  MockModbusIoProfile profile;
  RCR_REQUIRE(profile.write_digital_output(0, true, 10).accepted());
  const auto reply = profile.write_digital_output(0, false, 9);
  RCR_EXPECT(reply.status == ModbusIoCommandStatus::Rejected);
  RCR_EXPECT(profile.snapshot().digital_outputs[0].confirmed);
  RCR_EXPECT(profile.snapshot().last_update_monotonic_ns == 10);
}

} // namespace

RCR_TEST_MAIN()
