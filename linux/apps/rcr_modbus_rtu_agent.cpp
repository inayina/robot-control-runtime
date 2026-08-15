// Orange Pi 上的 Modbus RTU 主站。ThinkPad Qt 只连 TCP，不打开本机串口。
#include "rcr/workbench/application/modbus_agent_protocol.hpp"
#include "rcr/workbench/services/modbus_agent_server.hpp"
#include "rcr/workbench/services/physical_modbus_io_service.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool parse_u32(std::string_view text, std::uint32_t &value) {
  try {
    std::size_t used = 0;
    const unsigned long parsed = std::stoul(std::string(text), &used, 10);
    if (used != text.size() || parsed > 0xFFFFFFFFul) {
      return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

void usage(const char *program) {
  std::cerr
      << "usage: " << program
      << " [--serial /dev/ttyS7] [--baud 9600] [--slave 1]\n"
         "       [--listen 0.0.0.0:5740]\n"
         "\n"
         "Owns one RS-485/Modbus RTU master on this host and accepts a single\n"
         "commissioning TCP client. Not a Runtime remote endpoint.\n";
}

} // namespace

int main(int argc, char **argv) {
  rcr::workbench::PhysicalModbusIoConfig config;
  std::string bind = "0.0.0.0";
  std::uint16_t port = rcr::workbench::kModbusAgentDefaultPort;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return 0;
    }
    if (i + 1 >= argc) {
      usage(argv[0]);
      return 1;
    }
    const std::string_view value(argv[++i]);
    if (arg == "--serial") {
      config.serial_port = std::string(value);
    } else if (arg == "--baud") {
      std::uint32_t baud = 0;
      if (!parse_u32(value, baud)) {
        return 1;
      }
      config.baud_rate = baud;
    } else if (arg == "--slave") {
      std::uint32_t slave = 0;
      if (!parse_u32(value, slave) || slave < 1 || slave > 247) {
        return 1;
      }
      config.slave_id = static_cast<std::uint8_t>(slave);
    } else if (arg == "--listen") {
      const auto colon = value.rfind(':');
      if (colon == std::string_view::npos) {
        return 1;
      }
      bind = std::string(value.substr(0, colon));
      std::uint32_t parsed_port = 0;
      if (!parse_u32(value.substr(colon + 1), parsed_port) || parsed_port > 65535) {
        return 1;
      }
      port = static_cast<std::uint16_t>(parsed_port);
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  rcr::workbench::PhysicalModbusIoService service{config};
  rcr::workbench::ModbusAgentServer server{service};
  const auto listening = server.listen(bind, port);
  if (!listening) {
    std::cerr << "listen failed: " << listening.error().message() << '\n';
    return 2;
  }
  std::cerr << "modbus_rtu_agent listen=" << bind << ':' << server.port()
            << " serial=" << config.serial_port << " slave="
            << static_cast<int>(config.slave_id) << '\n';
  for (;;) {
    const auto served = server.serve_one(std::chrono::milliseconds{1000});
    if (!served && served.error().code() != rcr::Errc::Timeout) {
      std::cerr << "serve: " << served.error().message() << '\n';
    }
  }
}
