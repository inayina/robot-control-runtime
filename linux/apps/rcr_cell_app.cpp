// Orange Pi 边缘 Cell 应用：本进程是演示拓扑下 can0 的唯一写者。
// 不要并行再跑 rcrd；Qt 关掉后 CellReadyMapper 仍在这里 tick。
// 启动只 boot()，等工程站 Activate；Modbus 重连不自动重放 DO0。

#include "rcr/runtime_daemon.hpp"
#include "rcr/workbench/application/cell_app_protocol.hpp"
#include "rcr/workbench/application/cell_ready_mapper.hpp"
#include "rcr/workbench/application/runtime_application_adapter.hpp"
#include "rcr/workbench/services/cell_app_server.hpp"
#include "rcr/workbench/services/modbus_agent_client.hpp"
#include "rcr/workbench/application/modbus_agent_protocol.hpp"
#include "rcr/can_io_loop.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
  rcr::DaemonConfig daemon{};
  rcr::workbench::EvidenceClass evidence{
      rcr::workbench::EvidenceClass::Unspecified};
  std::string modbus_host{"127.0.0.1"};
  std::uint16_t modbus_port{rcr::workbench::kModbusAgentDefaultPort};
  std::string listen_host{"0.0.0.0"};
  std::uint16_t listen_port{rcr::workbench::kCellAppDefaultPort};
};

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

bool parse_host_port(std::string_view value, std::string &host,
                     std::uint16_t &port) {
  const auto colon = value.rfind(':');
  if (colon == std::string_view::npos || colon == 0) {
    return false;
  }
  std::uint32_t parsed_port = 0;
  if (!parse_u32(value.substr(colon + 1), parsed_port) || parsed_port > 65535) {
    return false;
  }
  host = std::string(value.substr(0, colon));
  port = static_cast<std::uint16_t>(parsed_port);
  return !host.empty();
}

void usage(const char *program) {
  std::cerr
      << "usage: " << program
      << " [--can can0] [--node-id 1] [--evidence vcan|physical]\n"
         "       [--modbus 127.0.0.1:5740] [--listen 0.0.0.0:5750]\n"
         "\n"
         "Owns SocketCAN in this process (stop board rcrd first). CellReadyMapper\n"
         "ticks even if ThinkPad Qt is closed. Does not auto-activate.\n";
}

class EdgeHandler final : public rcr::workbench::CellAppHandler {
public:
  explicit EdgeHandler(rcr::workbench::RuntimeApplicationAdapter &adapter)
      : adapter_(adapter) {}

  rcr::workbench::CellAppStatus status() override {
    auto snap = adapter_.snapshot();
    const auto cell = rcr::workbench::evaluate_cell_ready(snap);
    snap.position_reached = cell.position_reached;
    snap.cell_ready = cell.cell_ready;
    return rcr::workbench::project_cell_app_status(snap);
  }

  rcr::workbench::CommandReply activate() override {
    return adapter_.activate();
  }

  rcr::workbench::CommandReply
  submit_output(const rcr::workbench::DigitalOutputRequest &request) override {
    return adapter_.submit_digital_output(request);
  }

private:
  rcr::workbench::RuntimeApplicationAdapter &adapter_;
};

bool parse_options(int argc, char **argv, Options &options) {
  options.daemon.can_if = "can0";
  options.daemon.node_id = 1;
  options.daemon.period = std::chrono::milliseconds{10};
  // TARGET 在线上有效 2 s；100 ms 命令看门狗会在红外到位前把 Active 打进 Hold。
  options.daemon.command_timeout = std::chrono::milliseconds{2500};
  options.daemon.output_ack_timeout = std::chrono::milliseconds{500};
  options.daemon.heartbeat_timeout = std::chrono::milliseconds{300};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      std::exit(0);
    }
    if (i + 1 >= argc) {
      return false;
    }
    const std::string_view value(argv[++i]);
    if (arg == "--can") {
      options.daemon.can_if = std::string(value);
    } else if (arg == "--node-id") {
      std::uint32_t node = 0;
      if (!parse_u32(value, node) || node < 1 || node > 31) {
        return false;
      }
      options.daemon.node_id = static_cast<std::uint8_t>(node);
    } else if (arg == "--evidence") {
      if (value == "vcan") {
        options.evidence = rcr::workbench::EvidenceClass::Vcan;
      } else if (value == "physical") {
        options.evidence = rcr::workbench::EvidenceClass::Physical;
      } else {
        return false;
      }
    } else if (arg == "--modbus") {
      if (!parse_host_port(value, options.modbus_host, options.modbus_port)) {
        return false;
      }
    } else if (arg == "--listen") {
      if (!parse_host_port(value, options.listen_host, options.listen_port)) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

void tick_cell_ready(rcr::workbench::RuntimeApplicationAdapter &adapter,
                     rcr::workbench::CellReadyMapper &mapper,
                     rcr::workbench::ModbusAgentClient &modbus,
                     const std::string &modbus_host, std::uint16_t modbus_port) {
  auto snap = adapter.snapshot();
  const auto decision = rcr::workbench::evaluate_cell_ready(snap);
  // agent 空闲 1 s 无请求就会拆连接；不能把 TCP 占着等边沿。
  // observe 的 online 表示“允许写线圈”，不是“fd 已经连上”。
  const auto action = mapper.observe(decision, true);
  if (action == rcr::workbench::CellReadyDo0Action::None) {
    return;
  }
  if (!modbus.connected()) {
    auto connected = modbus.connect(modbus_host, modbus_port,
                                    std::chrono::milliseconds{200});
    if (!connected) {
      mapper.note_modbus_offline();
      return;
    }
  }
  const bool on = action == rcr::workbench::CellReadyDo0Action::RequestOn;
  auto written =
      modbus.write_output(0, on, std::chrono::milliseconds{1000});
  modbus.disconnect();
  if (!written) {
    mapper.note_modbus_offline();
  }
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    usage(argv[0]);
    return 1;
  }
  if (options.evidence == rcr::workbench::EvidenceClass::Unspecified) {
    std::cerr << "error: --evidence vcan|physical is required\n";
    return 1;
  }

  rcr::RuntimeDaemon daemon{options.daemon};
  const auto started = daemon.start();
  if (!started) {
    std::cerr << "rcr_cell_app start failed: " << started.error().message()
              << '\n';
    return static_cast<int>(daemon.exit_code());
  }
  const auto booted = daemon.boot();
  if (!booted.accepted) {
    std::cerr << "rcr_cell_app boot rejected: " << booted.reason << '\n';
    daemon.stop();
    return 4;
  }

  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {options.evidence, "SOCKETCAN"}};
  EdgeHandler handler{adapter};
  rcr::workbench::CellAppServer server{handler};
  const auto listening = server.listen(options.listen_host, options.listen_port);
  if (!listening) {
    std::cerr << "listen failed: " << listening.error().message() << '\n';
    daemon.stop();
    return 2;
  }

  rcr::workbench::CellReadyMapper mapper;
  rcr::workbench::ModbusAgentClient modbus;
  std::cerr << "rcr_cell_app can=" << options.daemon.can_if
            << " listen=" << options.listen_host << ':' << server.port()
            << " modbus=" << options.modbus_host << ':' << options.modbus_port
            << " evidence=" << rcr::workbench::to_string(options.evidence)
            << " (stop rcrd before using can0; mapper survives Qt close)\n";

  // 不把 wait_and_stop 放到并行线程：它会在 stop() 里销毁 runtime_，与本循环
  // snapshot 形成数据竞争。SIGINT 由 daemon 的 signalfd 收；I/O 停后再 join。
  while (daemon.started()) {
    const auto snap = daemon.snapshot();
    if (snap.io.stop_reason != rcr::IoStopReason::None || snap.stopping) {
      break;
    }
    tick_cell_ready(adapter, mapper, modbus, options.modbus_host,
                    options.modbus_port);
    const auto polled = server.poll(std::chrono::milliseconds{20});
    if (!polled && polled.error().code() != rcr::Errc::Timeout) {
      std::cerr << "cell serve: " << polled.error().message() << '\n';
    }
  }

  modbus.disconnect();
  server.close();
  daemon.request_stop();
  const auto code = daemon.wait_and_stop();
  std::cerr << "rcr_cell_app exit code=" << static_cast<int>(code) << " ("
            << rcr::to_string(code) << ")\n";
  return static_cast<int>(code);
}
