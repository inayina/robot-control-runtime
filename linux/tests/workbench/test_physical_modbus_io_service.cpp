#include "rcr/workbench/services/physical_modbus_io_service.hpp"

#include "rcr/workbench/services/modbus_rtu.hpp"
#include "test_support.hpp"

#include <chrono>
#include <span>
#include <vector>

namespace {

using rcr::workbench::PhysicalModbusIoService;
using rcr::Errc;

const std::vector<std::uint8_t> kLiveFc02Rx{0x01, 0x02, 0x01, 0x00, 0xa1, 0x88};

std::vector<std::uint8_t> rtu_with_crc(std::vector<std::uint8_t> body) {
  const auto crc = rcr::workbench::modbus_rtu_crc16(body);
  body.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  body.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFFu));
  return body;
}

rcr::Result<std::vector<std::uint8_t>>
mock_mr0(std::span<const std::uint8_t> request, std::chrono::milliseconds) {
  if (request.size() < 2) {
    return rcr::Error{Errc::InvalidArgument, "short RTU"};
  }
  switch (request[1]) {
  case rcr::workbench::kModbusFnReadDiscreteInputs:
    return kLiveFc02Rx;
  case rcr::workbench::kModbusFnReadCoils:
    return rtu_with_crc({0x01, 0x01, 0x01, 0x00});
  case rcr::workbench::kModbusFnWriteSingleCoil:
    return std::vector<std::uint8_t>(request.begin(), request.end());
  default:
    return rcr::Error{Errc::Rejected, "unexpected function"};
  }
}

RCR_TEST(probe_maps_online_from_injected_fc02) {
  PhysicalModbusIoService service{{}, mock_mr0};
  const auto snapshot = service.probe();
  RCR_REQUIRE(snapshot.ok());
  RCR_EXPECT(snapshot.value().backend == "PHYSICAL");
  RCR_EXPECT(snapshot.value().device_state ==
             rcr::workbench::ModbusDeviceState::Online);
  RCR_EXPECT(!snapshot.value().digital_inputs[0].active);
  RCR_EXPECT(!snapshot.value().digital_outputs[0].confirmed);
}

RCR_TEST(probe_maps_timeout_without_changing_di_to_fake_on) {
  PhysicalModbusIoService service{
      {}, [&](auto, auto) { return rcr::Error{Errc::Timeout, "no reply"}; }};
  const auto snapshot = service.probe();
  RCR_EXPECT(!snapshot.ok());
  RCR_EXPECT(service.snapshot().device_state ==
             rcr::workbench::ModbusDeviceState::Timeout);
  RCR_EXPECT(!service.snapshot().digital_inputs[0].active);
}

RCR_TEST(physical_does_not_claim_mock_evidence) {
  PhysicalModbusIoService service;
  RCR_EXPECT(service.snapshot().evidence == rcr::workbench::EvidenceClass::Physical);
  RCR_EXPECT(!service.snapshot().no_physical_rs485);
  RCR_EXPECT(service.snapshot().sku == "MR0-IOR08");
}

RCR_TEST(read_inputs_updates_di_without_confirming_do) {
  int fc02 = 0;
  PhysicalModbusIoService service{
      {}, [&](std::span<const std::uint8_t> request,
              std::chrono::milliseconds timeout)
          -> rcr::Result<std::vector<std::uint8_t>> {
        if (request.size() >= 2 &&
            request[1] == rcr::workbench::kModbusFnReadDiscreteInputs) {
          ++fc02;
          if (fc02 > 1) {
            return rtu_with_crc({0x01, 0x02, 0x01, 0x01});
          }
          return kLiveFc02Rx;
        }
        return mock_mr0(request, timeout);
      }};
  RCR_REQUIRE(service.probe().ok());
  RCR_EXPECT(!service.snapshot().digital_inputs[0].active);
  const auto polled = service.read_inputs();
  RCR_REQUIRE(polled.ok());
  RCR_EXPECT(polled.value().digital_inputs[0].active);
  RCR_EXPECT(!polled.value().digital_outputs[0].confirmed);
}

RCR_TEST(write_success_sets_confirmed) {
  PhysicalModbusIoService service{{}, mock_mr0};
  RCR_REQUIRE(service.probe().ok());
  const auto written = service.write_output(0, true);
  RCR_REQUIRE(written.ok());
  RCR_EXPECT(written.value().digital_outputs[0].requested);
  RCR_EXPECT(written.value().digital_outputs[0].confirmed);
  RCR_EXPECT(written.value().digital_outputs[0].last_status ==
             rcr::workbench::ModbusIoCommandStatus::Confirmed);
}

RCR_TEST(write_timeout_keeps_requested_and_does_not_confirm) {
  int writes = 0;
  PhysicalModbusIoService service{
      {}, [&](std::span<const std::uint8_t> request,
              std::chrono::milliseconds timeout)
          -> rcr::Result<std::vector<std::uint8_t>> {
        if (request.size() >= 2 &&
            request[1] == rcr::workbench::kModbusFnWriteSingleCoil) {
          ++writes;
          return rcr::Error{Errc::Timeout, "no coil echo"};
        }
        return mock_mr0(request, timeout);
      }};
  RCR_REQUIRE(service.probe().ok());
  const auto written = service.write_output(0, true);
  RCR_EXPECT(!written.ok());
  RCR_EXPECT(writes == 1);
  RCR_EXPECT(service.snapshot().digital_outputs[0].requested);
  RCR_EXPECT(!service.snapshot().digital_outputs[0].confirmed);
  RCR_EXPECT(service.snapshot().device_state ==
             rcr::workbench::ModbusDeviceState::Timeout);
  RCR_EXPECT(service.snapshot().digital_outputs[0].last_status ==
             rcr::workbench::ModbusIoCommandStatus::Timeout);
}

RCR_TEST(all_off_clears_confirmed_outputs) {
  PhysicalModbusIoService service{{}, mock_mr0};
  RCR_REQUIRE(service.probe().ok());
  RCR_REQUIRE(service.write_output(1, true).ok());
  const auto off = service.write_all_outputs_off();
  RCR_REQUIRE(off.ok());
  for (const auto &output : off.value().digital_outputs) {
    RCR_EXPECT(!output.requested);
    RCR_EXPECT(!output.confirmed);
  }
}

} // namespace

RCR_TEST_MAIN()
