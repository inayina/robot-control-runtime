// Qt 工具的组装入口（composition root），不是业务层，也不画控件。
//
// 本文件只做：解析命令行 → 启动 RuntimeDaemon → 接上 Adapter / Controller /
// MainWindow。CAN Health 判定、JSON 落盘、Mock 状态机都不写在这里。
//
// 所有权：daemon 在 main 栈上；Adapter / Controller / Window 放进内层 {}，
// 这样离开 {} 时 UI 和 worker 先拆掉，最后才 daemon.stop()。反了会在已停的
// Runtime 上取 snapshot。Window 只拿 Controller 的引用，不拥有 daemon。
//
// 对照笔记：docs/workbench/NOTES.md §6–§7。

#include "ui/main_window.hpp"
#include "controller/workbench_controller.hpp"

#include "rcr/runtime_daemon.hpp"
#include "rcr/workbench/application/runtime_application_adapter.hpp"
#include "rcr/workbench/services/cell_app_client.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

bool parse_node_id(const QString &text, std::uint8_t &node_id) {
  bool ok = false;
  const int value = text.toInt(&ok);
  if (!ok || value < 1 || value > 31) {
    return false;
  }
  node_id = static_cast<std::uint8_t>(value);
  return true;
}

std::string environment_or(const char *name, std::string fallback) {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  return value;
}

bool provenance_is_dirty() {
  const char *value = std::getenv("RCR_WORKBENCH_GIT_DIRTY");
  // 未显式证明 clean 时按 dirty 记录，避免普通启动被误当作正式证据。
  return value == nullptr || std::string_view{value} != "false";
}

bool parse_host_port(const QString &value, std::string &host, std::uint16_t &port) {
  const auto colon = value.lastIndexOf(QChar(':'));
  if (colon <= 0) {
    return false;
  }
  bool ok = false;
  const int parsed = value.mid(colon + 1).toInt(&ok);
  if (!ok || parsed <= 0 || parsed > 65535) {
    return false;
  }
  host = value.left(colon).toStdString();
  port = static_cast<std::uint16_t>(parsed);
  return !host.empty();
}

std::optional<rcr::workbench::EvidenceClass>
parse_evidence(const QString &value) {
  if (value == QStringLiteral("vcan")) {
    return rcr::workbench::EvidenceClass::Vcan;
  }
  if (value == QStringLiteral("physical")) {
    return rcr::workbench::EvidenceClass::Physical;
  }
  return std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
  // 必须最先构造：Qt 会从 argv 拿走平台插件参数，并创建 UI 线程的 event loop。
  // 还没有窗口；后面 Runtime 启动失败时直接非零退出，不假装界面正常。
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("rcr_qt_device_workbench"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Robot Edge Runtime and device commissioning workbench"));
  parser.addHelpOption();
  QCommandLineOption can_option{{QStringLiteral("c"), QStringLiteral("can")},
                                QStringLiteral("SocketCAN interface"),
                                QStringLiteral("interface"),
                                QStringLiteral("vcan0")};
  QCommandLineOption node_option{QStringLiteral("node-id"),
                                 QStringLiteral("CAN node id (1..31)"),
                                 QStringLiteral("id"), QStringLiteral("1")};
  QCommandLineOption results_option{
      QStringLiteral("results"), QStringLiteral("Result output directory"),
      QStringLiteral("directory"), QStringLiteral("workbench-results")};
  QCommandLineOption evidence_option{
      QStringLiteral("evidence"),
      QStringLiteral("Explicit evidence class: vcan or physical"),
      QStringLiteral("class")};
  QCommandLineOption cell_peer_option{
      QStringLiteral("cell-peer"),
      QStringLiteral("Orange Pi rcr_cell_app host:port (no local CAN)"),
      QStringLiteral("host:port")};
  QCommandLineOption modbus_peer_option{
      QStringLiteral("modbus-peer"),
      QStringLiteral("Modbus agent host:port for standalone PHYSICAL commissioning; "
                     "do not use with --cell-peer to own DO0"),
      QStringLiteral("host:port"), QStringLiteral("192.168.1.22:5740")};
  // 两个 *-once 给无显示器的 CI / Gate 用：走同一条 Controller 链，测完 quit。
  // 不能同时开——否则两个 quit/exit 会抢进程退出码。
  QCommandLineOption run_once_option{
      QStringLiteral("run-health-once"),
      QStringLiteral("Run CAN Health through Qt and exit on completion")};
  QCommandLineOption actuator_smoke_option{
      QStringLiteral("run-actuator-smoke-once"),
      QStringLiteral("Run isolated MOCK actuator smoke and exit")};
  QCommandLineOption show_lab_option{
      QStringLiteral("show-lab"),
      QStringLiteral("Show Lab / LOOPBACK and Lab / Actuator MOCK tabs")};
  parser.addOptions({can_option, node_option, results_option, evidence_option,
                     cell_peer_option, modbus_peer_option, show_lab_option,
                     run_once_option, actuator_smoke_option});
  parser.process(app);

  if (parser.isSet(run_once_option) && parser.isSet(actuator_smoke_option)) {
    std::cerr << "error: choose only one --run-*-once mode\n";
    return 2;
  }
  if (parser.isSet(cell_peer_option) && parser.isSet(run_once_option)) {
    std::cerr << "error: --run-health-once needs a local Runtime adapter\n";
    return 2;
  }

  std::uint8_t node_id = 0;
  if (!parse_node_id(parser.value(node_option), node_id)) {
    std::cerr << "error: --node-id must be 1..31\n";
    return 2;
  }

  const auto results_dir =
      QDir::cleanPath(parser.value(results_option)).toStdString();
  rcr::workbench::TestRunProvenance provenance{};
  provenance.git_commit = environment_or("RCR_WORKBENCH_GIT_COMMIT", "unknown");
  provenance.git_dirty = provenance_is_dirty();
  provenance.build_type =
      environment_or("RCR_WORKBENCH_BUILD_TYPE", "Qt6-unknown");

  auto attach_once_modes = [&](WorkbenchController &controller) {
    if (parser.isSet(modbus_peer_option)) {
      controller.setModbusAgentPeer(parser.value(modbus_peer_option));
    }
    if (parser.isSet(run_once_option)) {
      QObject::connect(&controller, &WorkbenchController::healthCompleted, &app,
                       &QCoreApplication::quit, Qt::QueuedConnection);
      QTimer::singleShot(0, &controller, &WorkbenchController::startHealth);
    } else if (parser.isSet(actuator_smoke_option)) {
      rcr::workbench::ActuatorSnapshot last_actuator{};
      QObject::connect(
          &controller, &WorkbenchController::actuatorSnapshotReady, &app,
          [&last_actuator](const rcr::workbench::ActuatorSnapshot &snapshot) {
            last_actuator = snapshot;
          });
      QTimer::singleShot(0, &controller, [&controller] {
        controller.driveEnable();
        controller.homeActuator();
      });
      QTimer::singleShot(650, &controller, [&controller] {
        controller.startActuatorVelocity(1.0);
      });
      QTimer::singleShot(1000, &controller,
                         &WorkbenchController::quickStopActuator);
      QTimer::singleShot(1450, &app, [&app, &last_actuator] {
        const bool passed =
            last_actuator.evidence == rcr::workbench::EvidenceClass::Mock &&
            last_actuator.isolated_mock && last_actuator.homed &&
            last_actuator.state == rcr::workbench::ActuatorState::Ready &&
            std::abs(last_actuator.actual_velocity_rad_s) <= 0.01 &&
            last_actuator.command_generation >= 4;
        std::cout << "actuator_mock_smoke=" << (passed ? "pass" : "failed")
                  << " state=" << rcr::workbench::to_string(last_actuator.state)
                  << " evidence="
                  << rcr::workbench::to_string(last_actuator.evidence) << '\n';
        app.exit(passed ? 0 : 5);
      });
    }
  };

  if (parser.isSet(cell_peer_option)) {
    std::string host;
    std::uint16_t port = 0;
    if (!parse_host_port(parser.value(cell_peer_option), host, port)) {
      std::cerr << "error: --cell-peer must be host:port\n";
      return 2;
    }
    rcr::workbench::CellAppClient cell_client;
    const auto connected =
        cell_client.connect(host, port, std::chrono::milliseconds{1000});
    if (!connected) {
      std::cerr << "error: cell-peer connect failed: "
                << connected.error().message() << '\n';
      return 3;
    }
    WorkbenchController controller{cell_client, provenance, results_dir};
    MainWindow window{controller, parser.isSet(show_lab_option)};
    controller.publishCurrentState();
    window.show();
    attach_once_modes(controller);
    return app.exec();
  }

  if (!parser.isSet(evidence_option)) {
    std::cerr << "error: --evidence vcan|physical is required\n";
    return 2;
  }
  const auto evidence = parse_evidence(parser.value(evidence_option));
  if (!evidence) {
    std::cerr << "error: --evidence must be vcan or physical\n";
    return 2;
  }

  // Runtime 在出窗口之前启动。失败则进程退出：没有 daemon 的空窗口没有工程价值。
  // period / heartbeat_timeout 是 Runtime 自己的时钟，和后面 Qt 的 100 ms 刷新无关。
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = parser.value(can_option).toStdString();
  daemon_config.node_id = node_id;
  daemon_config.period = std::chrono::milliseconds{10};
  daemon_config.heartbeat_timeout = std::chrono::milliseconds{300};
  rcr::RuntimeDaemon daemon{daemon_config};
  const auto started = daemon.start();
  if (!started) {
    std::cerr << "error: Runtime start failed: " << started.error().message()
              << '\n';
    return 3;
  }
  const auto booted = daemon.boot();
  if (!booted.accepted) {
    std::cerr << "error: Runtime boot rejected: " << booted.reason << '\n';
    daemon.stop();
    return 4;
  }

  int exit_code = 0;
  {
    // 证据等级必须由启动者写死，禁止根据 "vcan0" 这个名字猜是不是实物。
    // Adapter 只读 daemon，不 start/stop，也不打开第二套 SocketCAN。
    rcr::workbench::RuntimeApplicationAdapter adapter{
        daemon, {*evidence, "SOCKETCAN"}};
    WorkbenchController controller{adapter, provenance, results_dir};
    MainWindow window{controller, parser.isSet(show_lab_option)};
    // Window 已连接 signal 后再同步首帧，避免初始按钮与 DISABLED 状态短暂不一致。
    controller.publishCurrentState();
    window.show();
    attach_once_modes(controller);
    // 阻塞到窗口关闭，或 *-once 路径里 quit/exit。返回后离开 {}，再停 daemon。
    exit_code = app.exec();
  }

  daemon.stop();
  return exit_code;
}
