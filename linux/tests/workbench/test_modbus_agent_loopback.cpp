#include "rcr/workbench/services/modbus_agent_client.hpp"
#include "rcr/workbench/services/modbus_agent_server.hpp"
#include "rcr/workbench/services/modbus_rtu.hpp"
#include "rcr/workbench/services/physical_modbus_io_service.hpp"

#include "test_support.hpp"

#include <chrono>
#include <span>
#include <thread>
#include <vector>

namespace {

using rcr::workbench::ModbusAgentClient;
using rcr::workbench::ModbusAgentServer;
using rcr::workbench::PhysicalModbusIoService;

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
    return rcr::Error{rcr::Errc::InvalidArgument, "short RTU"};
  }
  switch (request[1]) {
  case rcr::workbench::kModbusFnReadDiscreteInputs:
    return kLiveFc02Rx;
  case rcr::workbench::kModbusFnReadCoils:
    return rtu_with_crc({0x01, 0x01, 0x01, 0x00});
  case rcr::workbench::kModbusFnWriteSingleCoil:
    return std::vector<std::uint8_t>(request.begin(), request.end());
  default:
    return rcr::Error{rcr::Errc::Rejected, "unexpected function"};
  }
}

RCR_TEST(localhost_probe_round_trip) {
  PhysicalModbusIoService service{{}, mock_mr0};
  ModbusAgentServer server{service};
  RCR_REQUIRE(server.listen("127.0.0.1", 0));
  const auto port = server.port();
  std::thread worker([&] {
    static_cast<void>(server.serve_one(std::chrono::milliseconds{2000}));
  });
  ModbusAgentClient client;
  const auto connected =
      client.connect("127.0.0.1", port, std::chrono::milliseconds{500});
  RCR_EXPECT(connected.ok());
  const auto snapshot = client.probe(std::chrono::milliseconds{1000});
  client.disconnect();
  worker.join();
  RCR_REQUIRE(snapshot.ok());
  RCR_EXPECT(snapshot.value().backend == "PHYSICAL");
  RCR_EXPECT(snapshot.value().device_state ==
             rcr::workbench::ModbusDeviceState::Online);
  RCR_EXPECT(snapshot.value().serial_port == "/dev/ttyS7");
}

RCR_TEST(localhost_session_read_and_write) {
  PhysicalModbusIoService service{{}, mock_mr0};
  ModbusAgentServer server{service};
  RCR_REQUIRE(server.listen("127.0.0.1", 0));
  const auto port = server.port();
  std::thread worker([&] {
    static_cast<void>(server.serve_one(std::chrono::milliseconds{2000}));
  });
  ModbusAgentClient client;
  RCR_REQUIRE(client.connect("127.0.0.1", port, std::chrono::milliseconds{500}));
  RCR_REQUIRE(client.probe(std::chrono::milliseconds{1000}));
  const auto di = client.read_inputs(std::chrono::milliseconds{1000});
  RCR_REQUIRE(di.ok());
  RCR_EXPECT(!di.value().digital_inputs[0].active);
  const auto written =
      client.write_output(0, true, std::chrono::milliseconds{1000});
  RCR_REQUIRE(written.ok());
  RCR_EXPECT(written.value().digital_outputs[0].requested);
  RCR_EXPECT(written.value().digital_outputs[0].confirmed);
  const auto off = client.write_all_outputs_off(std::chrono::milliseconds{2000});
  RCR_REQUIRE(off.ok());
  RCR_EXPECT(!off.value().digital_outputs[0].confirmed);
  client.disconnect();
  worker.join();
}

RCR_TEST(unconnected_client_does_not_silently_become_mock) {
  ModbusAgentClient client;
  const auto snapshot = client.probe(std::chrono::milliseconds{50});
  RCR_EXPECT(!snapshot.ok());
}

} // namespace

RCR_TEST_MAIN()
