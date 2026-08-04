#include "rcr_mbus/server.hpp"

#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Config {
  std::string host{"127.0.0.1"};
  std::uint16_t port{1502};
  std::uint16_t register_address{0};
  std::int16_t temperature_deci_c{255};
};

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " [--host 127.0.0.1] [--port 1502] [--register 0]"
               " [--temperature-deci-c 255]\n";
}

bool parse_i64(std::string_view text, std::int64_t& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

bool parse_config(int argc, char** argv, Config& cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return false;
    }
    if (i + 1 >= argc) {
      return false;
    }
    const std::string_view value{argv[++i]};
    if (arg == "--host") {
      if (value.empty()) {
        return false;
      }
      cfg.host = value;
      continue;
    }
    std::int64_t number = 0;
    if (!parse_i64(value, number)) {
      return false;
    }
    if (arg == "--port" && number >= 1 && number <= 65535) {
      cfg.port = static_cast<std::uint16_t>(number);
    } else if (arg == "--register" && number >= 0 && number < 64) {
      cfg.register_address = static_cast<std::uint16_t>(number);
    } else if (arg == "--temperature-deci-c" &&
               number >= std::numeric_limits<std::int16_t>::min() &&
               number <= std::numeric_limits<std::int16_t>::max()) {
      cfg.temperature_deci_c = static_cast<std::int16_t>(number);
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    }
  }
  Config cfg;
  if (!parse_config(argc, argv, cfg)) {
    usage(argv[0]);
    return 2;
  }

  rcr::mbus::ServerConfig server_cfg;
  server_cfg.bind_host = cfg.host;
  server_cfg.port = cfg.port;
  server_cfg.holding_count = 64;
  rcr::mbus::RefServer server{server_cfg};

  // 先写映像再启动服务线程，避免首个 client 与 seed 写入并发访问 HoldingMap。
  const auto raw = std::bit_cast<std::uint16_t>(cfg.temperature_deci_c);
  const auto seeded = server.map().write_single(cfg.register_address, raw);
  if (!seeded) {
    std::cerr << "seed failed: " << seeded.message << "\n";
    return 1;
  }
  const auto started = server.start();
  if (!started) {
    std::cerr << "start failed: " << started.message << "\n";
    return 1;
  }

  std::cout << "mbus_sensor_server listening on " << cfg.host << ":" << server.port()
            << " holding[" << cfg.register_address << "]=" << cfg.temperature_deci_c
            << " deci-C\n";
  while (server.running()) {
    std::this_thread::sleep_for(std::chrono::seconds{1});
  }
  return 0;
}
