#include "rcr/workbench/profile/mock_modbus_io_profile.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace rcr::workbench {

namespace {

constexpr std::string_view kMockPrefix{"MOCK / NO PHYSICAL RS485: "};

std::string mock_message(std::string_view message) {
  std::string result{kMockPrefix};
  result.append(message);
  return result;
}

} // namespace

MockModbusIoProfile::MockModbusIoProfile(MockModbusIoConfig config)
    : config_(std::move(config)) {
  snapshot_.slave_id = config_.primary_slave_id;
  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    snapshot_.digital_inputs[channel].channel =
        static_cast<std::uint8_t>(channel);
    snapshot_.digital_outputs[channel].channel =
        static_cast<std::uint8_t>(channel);
  }

  if (config_.primary_slave_id == 0 || config_.primary_slave_id > 247) {
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.last_error = mock_message("invalid primary slave id");
  }
}

ModbusIoSnapshot MockModbusIoProfile::snapshot() const { return snapshot_; }

bool MockModbusIoProfile::begin_scan(std::int64_t now_ns) {
  if (!update_time(now_ns)) {
    snapshot_.scan_state = ModbusScanState::Error;
    return false;
  }
  if (snapshot_.scan_state == ModbusScanState::Scanning) {
    snapshot_.last_error = mock_message("scan already in progress");
    return false;
  }
  snapshot_.scan_state = ModbusScanState::Scanning;
  snapshot_.slaves.clear();
  snapshot_.last_error.clear();
  return true;
}

bool MockModbusIoProfile::complete_scan(std::int64_t now_ns) {
  if (!update_time(now_ns)) {
    snapshot_.scan_state = ModbusScanState::Error;
    return false;
  }
  if (snapshot_.scan_state != ModbusScanState::Scanning) {
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.last_error = mock_message("complete_scan requires SCANNING");
    return false;
  }

  snapshot_.slaves = config_.scan_results;
  const auto primary =
      std::find_if(snapshot_.slaves.begin(), snapshot_.slaves.end(),
                   [this](const auto &slave) {
                     return slave.slave_id == config_.primary_slave_id;
                   });
  if (primary == snapshot_.slaves.end()) {
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.last_error =
        mock_message("primary slave missing from scan result");
    return false;
  }

  snapshot_.scan_state = ModbusScanState::Complete;
  snapshot_.device_state = primary->state;
  snapshot_.last_error.clear();
  return true;
}

ModbusIoCommandReply
MockModbusIoProfile::set_mock_digital_input(std::size_t channel, bool active,
                                            std::int64_t now_ns) {
  if (channel >= kModbusIoChannelCount) {
    return invalid_channel(channel, now_ns);
  }
  if (!update_time(now_ns)) {
    return {ModbusIoCommandStatus::Rejected, static_cast<std::uint8_t>(channel),
            active, snapshot_.digital_inputs[channel].active,
            mock_message("non-monotonic input timestamp rejected")};
  }
  snapshot_.digital_inputs[channel].active = active;
  snapshot_.last_error.clear();
  return {ModbusIoCommandStatus::Confirmed, static_cast<std::uint8_t>(channel),
          active, active,
          mock_message(active ? "DI injection confirmed ON"
                              : "DI injection confirmed OFF")};
}

ModbusIoCommandReply
MockModbusIoProfile::write_digital_output(std::size_t channel, bool active,
                                          std::int64_t now_ns) {
  if (channel >= kModbusIoChannelCount) {
    return invalid_channel(channel, now_ns);
  }
  if (!update_time(now_ns)) {
    return {ModbusIoCommandStatus::Rejected, static_cast<std::uint8_t>(channel),
            active, snapshot_.digital_outputs[channel].confirmed,
            mock_message("non-monotonic output timestamp rejected")};
  }

  auto &output = snapshot_.digital_outputs[channel];
  const bool confirmed_before = output.confirmed;
  output.requested = active;
  return finish_write(static_cast<std::uint8_t>(channel), active,
                      confirmed_before, consume_write_outcome());
}

ModbusIoCommandReply
MockModbusIoProfile::write_all_outputs_off(std::int64_t now_ns) {
  if (!update_time(now_ns)) {
    return {ModbusIoCommandStatus::Rejected, kAllModbusIoChannels, false, false,
            mock_message("non-monotonic all-off timestamp rejected")};
  }

  for (auto &output : snapshot_.digital_outputs) {
    output.requested = false;
  }
  const auto outcome = consume_write_outcome();
  if (outcome == ModbusIoCommandStatus::Confirmed) {
    for (auto &output : snapshot_.digital_outputs) {
      output.confirmed = false;
      output.last_status = outcome;
    }
  } else {
    for (auto &output : snapshot_.digital_outputs) {
      output.last_status = outcome;
    }
  }
  return finish_write(kAllModbusIoChannels, false, false, outcome);
}

void MockModbusIoProfile::set_next_write_outcome(
    ModbusIoCommandStatus outcome) {
  switch (outcome) {
  case ModbusIoCommandStatus::Confirmed:
  case ModbusIoCommandStatus::Timeout:
  case ModbusIoCommandStatus::Exception:
  case ModbusIoCommandStatus::Rejected:
    next_write_outcome_ = outcome;
    break;
  case ModbusIoCommandStatus::None:
  case ModbusIoCommandStatus::InvalidChannel:
    next_write_outcome_ = ModbusIoCommandStatus::Rejected;
    break;
  }
}

bool MockModbusIoProfile::update_time(std::int64_t now_ns) {
  if (now_ns < 0 || now_ns < snapshot_.last_update_monotonic_ns) {
    snapshot_.last_error = mock_message("monotonic timestamp moved backwards");
    return false;
  }
  snapshot_.last_update_monotonic_ns = now_ns;
  return true;
}

ModbusIoCommandStatus MockModbusIoProfile::consume_write_outcome() {
  const auto outcome = next_write_outcome_;
  next_write_outcome_ = ModbusIoCommandStatus::Confirmed;
  return outcome;
}

ModbusIoCommandReply MockModbusIoProfile::invalid_channel(std::size_t channel,
                                                          std::int64_t now_ns) {
  static_cast<void>(update_time(now_ns));
  snapshot_.last_error = mock_message("channel must be in range 0..3");
  const auto reported_channel =
      channel > std::numeric_limits<std::uint8_t>::max()
          ? kAllModbusIoChannels
          : static_cast<std::uint8_t>(channel);
  return {ModbusIoCommandStatus::InvalidChannel, reported_channel, false, false,
          snapshot_.last_error};
}

ModbusIoCommandReply
MockModbusIoProfile::finish_write(std::uint8_t channel, bool requested,
                                  bool confirmed_before,
                                  ModbusIoCommandStatus outcome) {
  bool confirmed = confirmed_before;
  if (channel != kAllModbusIoChannels) {
    auto &output = snapshot_.digital_outputs[channel];
    output.last_status = outcome;
    if (outcome == ModbusIoCommandStatus::Confirmed) {
      output.confirmed = requested;
    }
    confirmed = output.confirmed;
  }

  switch (outcome) {
  case ModbusIoCommandStatus::Confirmed:
    snapshot_.device_state = ModbusDeviceState::Online;
    snapshot_.last_error.clear();
    return {outcome, channel, requested, confirmed,
            mock_message("DO request confirmed")};
  case ModbusIoCommandStatus::Timeout:
    snapshot_.device_state = ModbusDeviceState::Timeout;
    snapshot_.last_error = mock_message("simulated write timeout");
    break;
  case ModbusIoCommandStatus::Exception:
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.last_error = mock_message("simulated Modbus exception");
    break;
  case ModbusIoCommandStatus::Rejected:
    snapshot_.last_error = mock_message("simulated write rejection");
    break;
  case ModbusIoCommandStatus::None:
  case ModbusIoCommandStatus::InvalidChannel:
    snapshot_.last_error = mock_message("invalid injected write outcome");
    break;
  }
  return {outcome, channel, requested, confirmed, snapshot_.last_error};
}

} // namespace rcr::workbench
