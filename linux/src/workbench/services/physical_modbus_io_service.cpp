#include "rcr/workbench/services/physical_modbus_io_service.hpp"

#include "rcr/time.hpp"
#include "rcr/workbench/services/modbus_rtu.hpp"

#include <utility>

namespace rcr::workbench {
namespace {

std::int64_t now_ns_or_zero() {
  const auto now = monotonic_now_ns();
  return now.ok() ? now.value() : 0;
}

} // namespace

PhysicalModbusIoService::PhysicalModbusIoService(PhysicalModbusIoConfig config,
                                                 RtuTransact transact)
    : config_(std::move(config)), transact_(std::move(transact)),
      using_injected_transact_(static_cast<bool>(transact_)) {
  apply_device_identity();
}

void PhysicalModbusIoService::apply_device_identity() {
  snapshot_.backend = "PHYSICAL";
  snapshot_.evidence = EvidenceClass::Physical;
  snapshot_.no_physical_rs485 = false;
  snapshot_.transport = "Modbus RTU";
  snapshot_.serial_port = config_.serial_port;
  snapshot_.sku = config_.sku;
  snapshot_.baud_rate = config_.baud_rate;
  snapshot_.baud_rate_placeholder = config_.baud_rate;
  snapshot_.parity = config_.parity == 'N' ? "None" : std::string(1, config_.parity);
  snapshot_.parity_placeholder = snapshot_.parity;
  snapshot_.slave_id = config_.slave_id;
  snapshot_.device_state = ModbusDeviceState::Unknown;
  snapshot_.scan_state = ModbusScanState::Unknown;
  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    snapshot_.digital_inputs[channel].channel =
        static_cast<std::uint8_t>(channel);
    snapshot_.digital_outputs[channel].channel =
        static_cast<std::uint8_t>(channel);
  }
}

void PhysicalModbusIoService::disconnect() noexcept {
  serial_.close();
  snapshot_.device_state = ModbusDeviceState::Unknown;
  snapshot_.scan_state = ModbusScanState::Unknown;
  snapshot_.last_error.clear();
}

void PhysicalModbusIoService::begin_transaction(
    std::uint8_t function, std::uint16_t address, std::uint16_t quantity,
    std::span<const std::uint8_t> tx) {
  snapshot_.last_error.clear();
  snapshot_.last_transaction.direction = "REQUEST";
  snapshot_.last_transaction.slave_id = config_.slave_id;
  snapshot_.last_transaction.function = function;
  snapshot_.last_transaction.address = address;
  snapshot_.last_transaction.quantity = quantity;
  snapshot_.last_transaction.tx_hex = bytes_to_hex(tx);
  snapshot_.last_transaction.rx_hex.clear();
  snapshot_.last_transaction.result.clear();
}

void PhysicalModbusIoService::apply_fault(const Error &error) {
  snapshot_.scan_state = ModbusScanState::Error;
  snapshot_.last_command_status = error.code() == Errc::Timeout
                                      ? ModbusIoCommandStatus::Timeout
                                      : ModbusIoCommandStatus::Rejected;
  snapshot_.device_state = error.code() == Errc::Timeout
                               ? ModbusDeviceState::Timeout
                               : ModbusDeviceState::Error;
  snapshot_.last_error = error.message();
  snapshot_.last_transaction.result = to_string(error.code());
}

Result<std::vector<std::uint8_t>>
PhysicalModbusIoService::transact(std::span<const std::uint8_t> request) {
  if (using_injected_transact_) {
    return transact_(request, config_.timeout);
  }
  if (!serial_.is_open()) {
    const auto opened = serial_.open(PosixSerialConfig{
        config_.serial_port, config_.baud_rate, config_.parity});
    if (!opened) {
      return opened.error();
    }
  }
  return serial_.transact(request, config_.timeout);
}

Result<ModbusIoSnapshot>
PhysicalModbusIoService::read_discrete_inputs(bool scanning) {
  if (scanning) {
    snapshot_.scan_state = ModbusScanState::Scanning;
  }
  const auto tx = encode_rtu_read(config_.slave_id, kModbusFnReadDiscreteInputs,
                                  kMr0IoRegisterStart, kMr0IoBitQuantity);
  begin_transaction(kModbusFnReadDiscreteInputs, kMr0IoRegisterStart,
                    kMr0IoBitQuantity, tx);

  const auto t0 = now_ns_or_zero();
  auto rx = transact(tx);
  const auto t1 = now_ns_or_zero();
  snapshot_.last_transaction.rtt_ns = (t1 > t0) ? (t1 - t0) : 0;
  snapshot_.last_update_monotonic_ns = t1;

  if (!rx) {
    apply_fault(rx.error());
    return rx.error();
  }

  snapshot_.last_transaction.rx_hex = bytes_to_hex(rx.value());
  auto decoded =
      decode_rtu_read_bits(rx.value(), config_.slave_id, kModbusFnReadDiscreteInputs);
  if (!decoded) {
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.last_command_status = ModbusIoCommandStatus::Exception;
    snapshot_.last_error = decoded.error().message();
    snapshot_.last_transaction.result = decoded.error().message();
    return decoded.error();
  }
  if (decoded.value().exception) {
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.last_command_status = ModbusIoCommandStatus::Exception;
    snapshot_.last_error =
        "Modbus exception " + std::to_string(decoded.value().exception_code);
    snapshot_.last_transaction.result = snapshot_.last_error;
    return Error{Errc::Rejected, snapshot_.last_error};
  }

  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    snapshot_.digital_inputs[channel].active =
        (decoded.value().bit_byte & (1u << channel)) != 0;
  }
  snapshot_.scan_state = ModbusScanState::Complete;
  snapshot_.device_state = ModbusDeviceState::Online;
  snapshot_.last_command_status = ModbusIoCommandStatus::Confirmed;
  snapshot_.last_transaction.result = "OK";
  snapshot_.last_transaction.direction = "RESPONSE";
  return snapshot_;
}

Result<void> PhysicalModbusIoService::refresh_coils() {
  const auto tx = encode_rtu_read(config_.slave_id, kModbusFnReadCoils,
                                  kMr0IoRegisterStart, kMr0IoBitQuantity);
  begin_transaction(kModbusFnReadCoils, kMr0IoRegisterStart, kMr0IoBitQuantity,
                    tx);
  const auto t0 = now_ns_or_zero();
  auto rx = transact(tx);
  const auto t1 = now_ns_or_zero();
  snapshot_.last_transaction.rtt_ns = (t1 > t0) ? (t1 - t0) : 0;
  snapshot_.last_update_monotonic_ns = t1;
  if (!rx) {
    return rx.error();
  }
  snapshot_.last_transaction.rx_hex = bytes_to_hex(rx.value());
  auto decoded =
      decode_rtu_read_bits(rx.value(), config_.slave_id, kModbusFnReadCoils);
  if (!decoded) {
    return decoded.error();
  }
  if (decoded.value().exception) {
    return Error{Errc::Rejected, "Modbus exception " +
                                     std::to_string(decoded.value().exception_code)};
  }
  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    const bool on = (decoded.value().bit_byte & (1u << channel)) != 0;
    // Probe 刷新实际线圈，requested 对齐 confirmed，避免恢复后重放旧 DO。
    snapshot_.digital_outputs[channel].confirmed = on;
    snapshot_.digital_outputs[channel].requested = on;
    snapshot_.digital_outputs[channel].last_status =
        ModbusIoCommandStatus::Confirmed;
  }
  snapshot_.last_transaction.result = "OK";
  snapshot_.last_transaction.direction = "RESPONSE";
  return Result<void>::success();
}

Result<ModbusIoSnapshot> PhysicalModbusIoService::probe() {
  auto di = read_discrete_inputs(true);
  if (!di) {
    return di;
  }
  // FC01 只刷新实际线圈；失败不把已经确认的 DI ONLINE 打成 TIMEOUT。
  const auto coils = refresh_coils();
  if (!coils) {
    snapshot_.last_error = std::string("coil refresh: ") + coils.error().message();
    snapshot_.last_transaction.result = snapshot_.last_error;
  }
  snapshot_.device_state = ModbusDeviceState::Online;
  snapshot_.scan_state = ModbusScanState::Complete;
  snapshot_.last_command_status = ModbusIoCommandStatus::Confirmed;
  return snapshot_;
}

Result<ModbusIoSnapshot> PhysicalModbusIoService::read_inputs() {
  return read_discrete_inputs(false);
}

Result<ModbusIoSnapshot>
PhysicalModbusIoService::write_output(std::size_t channel, bool active) {
  if (channel >= kModbusIoChannelCount) {
    snapshot_.last_command_status = ModbusIoCommandStatus::InvalidChannel;
    snapshot_.last_error = "DO channel must be in range 0..3";
    return Error{Errc::InvalidArgument, snapshot_.last_error};
  }
  auto &output = snapshot_.digital_outputs[channel];
  output.requested = active;
  output.last_status = ModbusIoCommandStatus::None;

  const auto address = static_cast<std::uint16_t>(channel);
  const auto tx =
      encode_rtu_write_single_coil(config_.slave_id, address, active);
  begin_transaction(kModbusFnWriteSingleCoil, address, 1, tx);

  const auto t0 = now_ns_or_zero();
  auto rx = transact(tx);
  const auto t1 = now_ns_or_zero();
  snapshot_.last_transaction.rtt_ns = (t1 > t0) ? (t1 - t0) : 0;
  snapshot_.last_update_monotonic_ns = t1;

  if (!rx) {
    apply_fault(rx.error());
    output.last_status = snapshot_.last_command_status;
    return rx.error();
  }
  snapshot_.last_transaction.rx_hex = bytes_to_hex(rx.value());
  auto decoded = decode_rtu_write_single_coil(rx.value(), config_.slave_id,
                                              address, active);
  if (!decoded) {
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.last_command_status = ModbusIoCommandStatus::Exception;
    snapshot_.last_error = decoded.error().message();
    snapshot_.last_transaction.result = decoded.error().message();
    output.last_status = ModbusIoCommandStatus::Exception;
    return decoded.error();
  }
  if (decoded.value().exception) {
    snapshot_.scan_state = ModbusScanState::Error;
    snapshot_.device_state = ModbusDeviceState::Error;
    snapshot_.last_command_status = ModbusIoCommandStatus::Exception;
    snapshot_.last_error =
        "Modbus exception " + std::to_string(decoded.value().exception_code);
    snapshot_.last_transaction.result = snapshot_.last_error;
    output.last_status = ModbusIoCommandStatus::Exception;
    return Error{Errc::Rejected, snapshot_.last_error};
  }

  output.confirmed = active;
  output.last_status = ModbusIoCommandStatus::Confirmed;
  snapshot_.scan_state = ModbusScanState::Complete;
  snapshot_.device_state = ModbusDeviceState::Online;
  snapshot_.last_command_status = ModbusIoCommandStatus::Confirmed;
  snapshot_.last_transaction.result = "OK";
  snapshot_.last_transaction.direction = "RESPONSE";
  return snapshot_;
}

Result<ModbusIoSnapshot> PhysicalModbusIoService::write_all_outputs_off() {
  // FC0F 尚未 live-verify；ALL OFF 连发已验证路径上的 FC05，中途失败立即停。
  Result<ModbusIoSnapshot> last = snapshot_;
  for (std::size_t channel = 0; channel < kModbusIoChannelCount; ++channel) {
    last = write_output(channel, false);
    if (!last) {
      return last;
    }
  }
  return last;
}

} // namespace rcr::workbench
