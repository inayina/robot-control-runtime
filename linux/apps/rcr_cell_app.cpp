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

// 边缘记住最近一次 DO0 事务，经 CEL1 给工程站展示；Qt 不另开自动写线圈。
struct EdgeCellIoState {
  bool modbus_online{false};
  bool do0_requested{false};
  bool do0_confirmed{false};
  rcr::workbench::ModbusIoCommandStatus do0_status{
      rcr::workbench::ModbusIoCommandStatus::None};
};

class EdgeHandler final : public rcr::workbench::CellAppHandler {
public:
  EdgeHandler(rcr::workbench::RuntimeApplicationAdapter &adapter,
              rcr::workbench::CellReadyMapper &mapper,
              rcr::workbench::ModbusAgentClient &modbus, EdgeCellIoState &io,
              const std::string &modbus_host, std::uint16_t modbus_port)
      : adapter_(adapter), mapper_(mapper), modbus_(modbus), io_(io),
        modbus_host_(modbus_host), modbus_port_(modbus_port) {}

  rcr::workbench::CellAppStatus status() override {
    auto snap = adapter_.snapshot();
    const auto cell = rcr::workbench::evaluate_cell_ready(snap);
    snap.position_reached = cell.position_reached;
    snap.cell_ready = cell.cell_ready;
    snap.cell_modbus_online = io_.modbus_online;
    snap.cell_ready_do0_requested = io_.do0_requested;
    snap.cell_ready_do0_confirmed = io_.do0_confirmed;
    snap.cell_ready_do0_status =
        static_cast<std::uint8_t>(io_.do0_status);
    return rcr::workbench::project_cell_app_status(snap);
  }

  rcr::workbench::CellAppStatus probe_cell_io() override {
    // CEL1 Probe 是工程师显式触发的恢复检查，不是 timer retry。主循环和 handler
    // 都在本线程使用这个 client；bounded I/O 不会改变 RuntimeDaemon/CAN ownership。
    if (!modbus_.connected()) {
      const auto connected = modbus_.connect(modbus_host_, modbus_port_,
                                             std::chrono::milliseconds{200});
      if (!connected) {
        mapper_.note_modbus_offline();
        io_.modbus_online = false;
        io_.do0_status = rcr::workbench::ModbusIoCommandStatus::Timeout;
        return status();
      }
    }
    const auto probed = modbus_.probe(std::chrono::milliseconds{1000});
    modbus_.disconnect();
    if (!probed) {
      mapper_.note_modbus_offline();
      io_.modbus_online = false;
      io_.do0_status = rcr::workbench::ModbusIoCommandStatus::Timeout;
      return status();
    }

    const auto &do0 = probed.value().digital_outputs[0];
    io_.modbus_online = true;
    io_.do0_requested = do0.requested;
    io_.do0_confirmed = do0.confirmed;
    io_.do0_status = do0.last_status;
    // 成功后只同步边沿基准：Probe 的 FC02/FC01 结果绝不能触发 FC05。
    mapper_.synchronize_after_probe(
        rcr::workbench::evaluate_cell_ready(adapter_.snapshot()));
    return status();
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
  rcr::workbench::CellReadyMapper &mapper_;
  rcr::workbench::ModbusAgentClient &modbus_;
  EdgeCellIoState &io_;
  const std::string &modbus_host_;
  std::uint16_t modbus_port_{0};
};

bool parse_options(int argc, char **argv, Options &options) {
  options.daemon.can_if = "can0";
  options.daemon.node_id = 1;
  options.daemon.period = std::chrono::milliseconds{10};
  // 在途 HOME/TARGET 的 lease 约 2 s；命令看门狗只打在途命令，不再把空闲 Active 打进 Hold。
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
                     EdgeCellIoState &io, const std::string &modbus_host,
                     std::uint16_t modbus_port) {
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
      io.modbus_online = false;
      io.do0_status = rcr::workbench::ModbusIoCommandStatus::Timeout;
      return;
    }
  }
  const bool on = action == rcr::workbench::CellReadyDo0Action::RequestOn;
  io.do0_requested = on;
  auto written =
      modbus.write_output(0, on, std::chrono::milliseconds{1000});
  modbus.disconnect();
  if (!written) {
    mapper.note_modbus_offline();
    io.modbus_online = false;
    io.do0_status = rcr::workbench::ModbusIoCommandStatus::Timeout;
    return;
  }
  io.modbus_online = true;
  const auto &ch = written.value().digital_outputs[0];
  io.do0_requested = ch.requested;
  io.do0_confirmed = ch.confirmed;
  io.do0_status = ch.last_status;
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
  EdgeCellIoState cell_io{};
  rcr::workbench::CellReadyMapper mapper;
  rcr::workbench::ModbusAgentClient modbus;
  EdgeHandler handler{adapter, mapper, modbus, cell_io, options.modbus_host,
                      options.modbus_port};
  rcr::workbench::CellAppServer server{handler};
  const auto listening = server.listen(options.listen_host, options.listen_port);
  if (!listening) {
    std::cerr << "listen failed: " << listening.error().message() << '\n';
    daemon.stop();
    return 2;
  }

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
    // 先服务 CEL1：mapper 写线圈可能阻塞约 1 s，若放在 poll 前，工程站 80 ms
    // GetStatus / Activate 会超时，按钮看起来没反应。
    const auto polled = server.poll(std::chrono::milliseconds{20});
    if (!polled && polled.error().code() != rcr::Errc::Timeout) {
      std::cerr << "cell serve: " << polled.error().message() << '\n';
    }
    tick_cell_ready(adapter, mapper, modbus, cell_io, options.modbus_host,
                    options.modbus_port);
  }

  modbus.disconnect();
  server.close();
  daemon.request_stop();
  const auto code = daemon.wait_and_stop();
  std::cerr << "rcr_cell_app exit code=" << static_cast<int>(code) << " ("
            << rcr::to_string(code) << ")\n";
  return static_cast<int>(code);
}
