// rcrd：可部署 Runtime daemon。只经 SocketCAN 监督配置节点；不自动发演示输出。
#include "rcr/runtime_daemon.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include <sched.h>

namespace {

bool parse_i64(std::string_view text, std::int64_t& value) {
  try {
    std::size_t used = 0;
    value = std::stoll(std::string(text), &used, 10);
    return used == text.size();
  } catch (...) {
    return false;
  }
}

void usage(const char* program) {
  std::cerr
      << "usage: " << program
      << " [--can IFACE] [--node-id 1..31] [--period-ms N]\n"
         "       [--command-timeout-ms N] [--heartbeat-timeout-ms N]\n"
         "       [--fifo-priority 0..99] [--require-fifo] [--cpu-affinity N]\n"
         "       [--duration-ms N]\n"
         "\n"
         "Supervises one CAN node over an existing interface (see setup_vcan.sh).\n"
         "Does not auto-send demo outputs. SIGINT/SIGTERM exit 0 after bounded shutdown.\n"
         "Exit codes: 0=ok 1=config 2=interface 3=permission 4=worker\n"
         "YAML config is not loaded in P1; linux/configs/runtime_v1.yaml remains a draft.\n";
}

bool parse_options(int argc, char** argv, rcr::DaemonConfig& config, bool& help) {
  help = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      help = true;
      return true;
    }
    if (arg == "--require-fifo") {
      config.require_fifo = true;
      continue;
    }
    if (i + 1 >= argc) {
      return false;
    }
    const std::string_view value(argv[++i]);
    std::int64_t wide = 0;
    if (arg == "--can") {
      config.can_if = std::string(value);
    } else if (arg == "--node-id") {
      if (!parse_i64(value, wide) || wide < 1 || wide > 31) {
        return false;
      }
      config.node_id = static_cast<std::uint8_t>(wide);
    } else if (arg == "--period-ms") {
      if (!parse_i64(value, wide) || wide < 1 || wide > 60'000) {
        return false;
      }
      config.period = std::chrono::milliseconds{wide};
    } else if (arg == "--command-timeout-ms") {
      if (!parse_i64(value, wide) || wide < 1 || wide > 60'000) {
        return false;
      }
      config.command_timeout = std::chrono::milliseconds{wide};
    } else if (arg == "--heartbeat-timeout-ms") {
      if (!parse_i64(value, wide) || wide < 1 || wide > 60'000) {
        return false;
      }
      config.heartbeat_timeout = std::chrono::milliseconds{wide};
    } else if (arg == "--fifo-priority") {
      if (!parse_i64(value, wide) || wide < 0 || wide > 99) {
        return false;
      }
      config.fifo_priority = static_cast<int>(wide);
    } else if (arg == "--cpu-affinity") {
      if (!parse_i64(value, wide) || wide < 0 || wide >= CPU_SETSIZE) {
        return false;
      }
      config.cpu_affinity = static_cast<int>(wide);
    } else if (arg == "--duration-ms") {
      if (!parse_i64(value, wide) || wide < 0 || wide > 3'600'000) {
        return false;
      }
      config.duration = std::chrono::milliseconds{wide};
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  rcr::DaemonConfig config{};
  bool help = false;
  if (!parse_options(argc, argv, config, help)) {
    usage(argv[0]);
    return static_cast<int>(rcr::DaemonExitCode::ConfigError);
  }
  if (help) {
    usage(argv[0]);
    return static_cast<int>(rcr::DaemonExitCode::Ok);
  }

  rcr::RuntimeDaemon daemon(config);
  auto started = daemon.start();
  if (!started) {
    std::cerr << "rcrd start failed: " << started.error().message() << '\n';
    return static_cast<int>(daemon.exit_code());
  }

  const auto code = daemon.wait_and_stop();
  std::cerr << "rcrd exit code=" << static_cast<int>(code) << " ("
            << rcr::to_string(code) << ")\n";
  return static_cast<int>(code);
}
