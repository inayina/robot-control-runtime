#include "controller/workbench_controller.hpp"
#include "ui/main_window.hpp"

#include "rcr/runtime_daemon.hpp"
#include "rcr/workbench/application/runtime_application_adapter.hpp"
#include "rcr/workbench/services/cell_app_client.hpp"
#include "rcr/workbench/services/cell_app_server.hpp"
#include "rcr/workbench/services/modbus_rtu.hpp"
#include "rcr/workbench/services/modbus_agent_server.hpp"
#include "rcr/workbench/services/physical_modbus_io_service.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

rcr::workbench::TestRunProvenance provenance() {
  return {"qt-test", true, "Debug"};
}

int tab_index_named(QTabWidget *tabs, const QString &title) {
  for (int i = 0; i < tabs->count(); ++i) {
    if (tabs->tabText(i) == title) {
      return i;
    }
  }
  return -1;
}

const std::vector<std::uint8_t> kLiveFc02Off{0x01, 0x02, 0x01, 0x00, 0xa1, 0x88};

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
    return kLiveFc02Off;
  case rcr::workbench::kModbusFnReadCoils:
    return rtu_with_crc({0x01, 0x01, 0x01, 0x00});
  case rcr::workbench::kModbusFnWriteSingleCoil:
    return std::vector<std::uint8_t>(request.begin(), request.end());
  default:
    return rcr::Error{rcr::Errc::Rejected, "unexpected function"};
  }
}

} // namespace

class QtWorkbenchTest final : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void rendersExplicitEvidence();
  void synchronizesInitialControls();
  void routesMockCommandsAndJogRelease();
  void routesMockModbusScanDiAndDoReplies();
  void routesRemoteLoopbackConnectionPage();
  void restoresHealthButtonsAfterWorkerCompletion();
  void runsCellPeerCanHealthAgainstCel1();
  void routesPhysicalModbusProbeThroughWorker();
  void routesPhysicalModbusDiPollAndDoConfirm();
};

void QtWorkbenchTest::rendersExplicitEvidence() {
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "can-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Physical, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};

  controller.publishCurrentState();

  QCOMPARE(window.windowTitle(),
           QStringLiteral("Robot Edge Runtime & Device Commissioning Workbench"));
  const auto *backend = window.findChild<QLabel *>("backendEvidenceValue");
  QVERIFY(backend != nullptr);
  QCOMPARE(backend->text(), QStringLiteral("SOCKETCAN / PHYSICAL"));

  // daemon 未启动会令 Health 返回 ERROR，但 prepare 已记录环境；这里验证
  // PHYSICAL 不只停在标签，而是同样进入 worker 的测试 criteria/result。
  QSignalSpy completed{&controller, &WorkbenchController::healthCompleted};
  controller.startHealth();
  QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
  const auto arguments = completed.takeFirst();
  const auto result =
      qvariant_cast<rcr::workbench::TestResult>(arguments.at(0));
  QVERIFY(result.environment.evidence ==
          rcr::workbench::EvidenceClass::Physical);
}

void QtWorkbenchTest::synchronizesInitialControls() {
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};
  controller.publishCurrentState();
  window.show();

  auto *tabs = window.findChild<QTabWidget *>("workbenchTabs");
  QVERIFY(tabs != nullptr);
  QCOMPARE(tabs->count(), 4);
  QCOMPARE(tabs->tabText(0), QStringLiteral("Overview"));
  QCOMPARE(tabs->tabText(1), QStringLiteral("Runtime"));
  QCOMPARE(tabs->tabText(2), QStringLiteral("Cell I/O"));
  QCOMPARE(tabs->tabText(3), QStringLiteral("Verification"));
  QCOMPARE(tab_index_named(tabs, QStringLiteral("Lab / LOOPBACK")), -1);
  QCOMPARE(tab_index_named(tabs, QStringLiteral("Lab / Actuator MOCK")), -1);
  QVERIFY(window.findChild<QPushButton *>("activateRuntimeButton") != nullptr);
  QVERIFY(window.findChild<QPushButton *>("commandServoHomeButton") != nullptr);
  QVERIFY(window.findChild<QLabel *>("overviewCellReadyValue") != nullptr);
  auto *activate = window.findChild<QPushButton *>("activateRuntimeButton");
  auto *last_command = window.findChild<QLabel *>("lastCommandReplyValue");
  QVERIFY(activate != nullptr);
  QVERIFY(last_command != nullptr);
  QCOMPARE(last_command->text(), QStringLiteral("NONE"));
  activate->click();
  QCoreApplication::processEvents();
  QVERIFY(last_command->text().startsWith(QStringLiteral("REJECTED")));

  const auto *state = window.findChild<QLabel *>("actuatorStateValue");
  const auto *enable = window.findChild<QPushButton *>("driveEnableButton");
  const auto *home = window.findChild<QPushButton *>("homeActuatorButton");
  const auto *start = window.findChild<QPushButton *>("startActuatorButton");
  const auto *jog = window.findChild<QPushButton *>("jogPositiveButton");
  const auto *quick_stop = window.findChild<QPushButton *>("quickStopButton");
  QVERIFY(state != nullptr);
  QVERIFY(enable != nullptr);
  QVERIFY(home != nullptr);
  QVERIFY(start != nullptr);
  QVERIFY(jog != nullptr);
  QVERIFY(quick_stop != nullptr);
  QCOMPARE(state->text(), QStringLiteral("DISABLED"));
  QVERIFY(enable->isEnabled());
  const auto *position = window.findChild<QLabel *>("overviewPositionReachedValue");
  const auto *cell_ready = window.findChild<QLabel *>("overviewCellReadyValue");
  const auto *do0_requested =
      window.findChild<QLabel *>("overviewDo0RequestedValue");
  const auto *do0_confirmed =
      window.findChild<QLabel *>("overviewDo0ConfirmedValue");
  QVERIFY(position != nullptr);
  QVERIFY(cell_ready != nullptr);
  QVERIFY(do0_requested != nullptr);
  QVERIFY(do0_confirmed != nullptr);
  QCOMPARE(position->text(), QStringLiteral("NOT REACHED"));
  QCOMPARE(cell_ready->text(), QStringLiteral("FALSE"));
  QCOMPARE(do0_requested->text(), QStringLiteral("OFF"));
  QCOMPARE(do0_confirmed->text(), QStringLiteral("OFF"));
  const auto *do0_request = window.findChild<QCheckBox *>("modbusDo0Request");
  QVERIFY(do0_request != nullptr);
  QVERIFY(!do0_request->isEnabled());
  QVERIFY(!home->isEnabled());
  QVERIFY(!start->isEnabled());
  QVERIFY(!jog->isEnabled());
  QVERIFY(!quick_stop->isEnabled());
}

void QtWorkbenchTest::routesMockCommandsAndJogRelease() {
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller, true};
  controller.publishCurrentState();
  window.show();

  auto *tabs = window.findChild<QTabWidget *>("workbenchTabs");
  QVERIFY(tabs != nullptr);
  QCOMPARE(tabs->count(), 6);
  QCOMPARE(tab_index_named(tabs, QStringLiteral("Lab / Actuator MOCK")), 5);
  tabs->setCurrentIndex(tab_index_named(tabs, QStringLiteral("Lab / Actuator MOCK")));
  QCoreApplication::processEvents();

  auto *enable = window.findChild<QPushButton *>("driveEnableButton");
  auto *home = window.findChild<QPushButton *>("homeActuatorButton");
  auto *jog = window.findChild<QPushButton *>("jogPositiveButton");
  auto *state = window.findChild<QLabel *>("actuatorStateValue");
  QVERIFY(enable != nullptr);
  QVERIFY(home != nullptr);
  QVERIFY(jog != nullptr);
  QVERIFY(state != nullptr);

  QTest::mouseClick(enable, Qt::LeftButton);
  QTRY_COMPARE(state->text(), QStringLiteral("IDLE"));
  QVERIFY(home->isEnabled());

  QTest::mouseClick(home, Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(jog->isEnabled(), 1500);
  QCOMPARE(state->text(), QStringLiteral("READY"));

  QTest::mousePress(jog, Qt::LeftButton);
  // offscreen 插件可能在一次合成鼠标事件后立即释放 grab；两种状态都证明
  // pressed 已进入 Jog，且 release/lease cleanup 没有把 Mock 留在运动中。
  QVERIFY(state->text() == QStringLiteral("RUNNING") ||
          state->text() == QStringLiteral("STOPPING"));
  QTest::mouseRelease(jog, Qt::LeftButton);
  QTRY_COMPARE_WITH_TIMEOUT(state->text(), QStringLiteral("READY"), 1000);
}

void QtWorkbenchTest::routesMockModbusScanDiAndDoReplies() {
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};
  controller.publishCurrentState();
  window.show();

  auto *tabs = window.findChild<QTabWidget *>("workbenchTabs");
  QVERIFY(tabs != nullptr);
  QCOMPARE(tab_index_named(tabs, QStringLiteral("Cell I/O")), 2);
  tabs->setCurrentIndex(tab_index_named(tabs, QStringLiteral("Cell I/O")));
  QCoreApplication::processEvents();

  auto *banner = window.findChild<QLabel *>("modbusMockBanner");
  auto *scan = window.findChild<QPushButton *>("modbusScanButton");
  auto *scan_summary = window.findChild<QLabel *>("modbusScanSummary");
  auto *di_inject = window.findChild<QCheckBox *>("modbusDi2Inject");
  auto *di_value = window.findChild<QLabel *>("modbusDi2Value");
  auto *do0_request = window.findChild<QCheckBox *>("modbusDo0Request");
  auto *do_request = window.findChild<QCheckBox *>("modbusDo1Request");
  auto *do_requested = window.findChild<QLabel *>("modbusDo1Requested");
  auto *do_confirmed = window.findChild<QLabel *>("modbusDo1Confirmed");
  auto *do_status = window.findChild<QLabel *>("modbusDo1Status");
  auto *all_off = window.findChild<QPushButton *>("modbusAllOffButton");
  auto *reply = window.findChild<QLabel *>("modbusReplyValue");
  QVERIFY(banner != nullptr);
  QVERIFY(scan != nullptr);
  QVERIFY(scan_summary != nullptr);
  QVERIFY(di_inject != nullptr);
  QVERIFY(di_value != nullptr);
  QVERIFY(do0_request != nullptr);
  QVERIFY(do_request != nullptr);
  QVERIFY(do_requested != nullptr);
  QVERIFY(do_confirmed != nullptr);
  QVERIFY(do_status != nullptr);
  QVERIFY(all_off != nullptr);
  QVERIFY(reply != nullptr);
  QVERIFY(!do0_request->isEnabled());
  QVERIFY(banner->text().contains(QStringLiteral("NO PHYSICAL RS485")));

  scan->click();
  QTRY_VERIFY(
      scan_summary->text().contains(QStringLiteral("Slave 2  TIMEOUT")));

  di_inject->click();
  QTRY_COMPARE(di_value->text(), QStringLiteral("● ON"));

  do_request->click();
  QTRY_COMPARE(do_requested->text(), QStringLiteral("ON"));
  QCOMPARE(do_confirmed->text(), QStringLiteral("ON"));
  QCOMPARE(do_status->text(), QStringLiteral("CONFIRMED"));

  controller.setNextMockModbusWriteOutcome(
      rcr::workbench::ModbusIoCommandStatus::Timeout);
  do_request->click();
  QTRY_COMPARE(do_requested->text(), QStringLiteral("OFF"));
  QCOMPARE(do_confirmed->text(), QStringLiteral("ON"));
  QCOMPARE(do_status->text(), QStringLiteral("TIMEOUT"));
  QVERIFY(reply->text().contains(QStringLiteral("NO PHYSICAL RS485")));

  QSignalSpy command_replies{&controller,
                             &WorkbenchController::modbusCommandCompleted};

  controller.setNextMockModbusWriteOutcome(
      rcr::workbench::ModbusIoCommandStatus::Exception);
  controller.requestDigitalOutput(1, false);
  QCOMPARE(command_replies.count(), 1);
  auto failed = qvariant_cast<rcr::workbench::ModbusIoCommandReply>(
      command_replies.takeFirst().at(0));
  QCOMPARE(failed.status, rcr::workbench::ModbusIoCommandStatus::Exception);
  QCOMPARE(do_confirmed->text(), QStringLiteral("ON"));
  QCOMPARE(do_status->text(), QStringLiteral("EXCEPTION"));

  controller.setNextMockModbusWriteOutcome(
      rcr::workbench::ModbusIoCommandStatus::Rejected);
  controller.requestDigitalOutput(1, false);
  QCOMPARE(command_replies.count(), 1);
  failed = qvariant_cast<rcr::workbench::ModbusIoCommandReply>(
      command_replies.takeFirst().at(0));
  QCOMPARE(failed.status, rcr::workbench::ModbusIoCommandStatus::Rejected);
  QCOMPARE(do_confirmed->text(), QStringLiteral("ON"));
  QCOMPARE(do_status->text(), QStringLiteral("REJECTED"));

  all_off->click();
  QTRY_COMPARE(do_requested->text(), QStringLiteral("OFF"));
  QCOMPARE(do_confirmed->text(), QStringLiteral("OFF"));
  QCOMPARE(do_status->text(), QStringLiteral("CONFIRMED"));
  QCOMPARE(command_replies.count(), 1);
  const auto all_off_reply =
      qvariant_cast<rcr::workbench::ModbusIoCommandReply>(
          command_replies.takeFirst().at(0));
  QCOMPARE(all_off_reply.status,
           rcr::workbench::ModbusIoCommandStatus::Confirmed);
  QCOMPARE(all_off_reply.channel, rcr::workbench::kAllModbusIoChannels);

  controller.requestDigitalOutput(4, true);
  QCOMPARE(command_replies.count(), 1);
  const auto invalid = qvariant_cast<rcr::workbench::ModbusIoCommandReply>(
      command_replies.takeFirst().at(0));
  QCOMPARE(invalid.status,
           rcr::workbench::ModbusIoCommandStatus::InvalidChannel);
  QCOMPARE(do_confirmed->text(), QStringLiteral("OFF"));
}

void QtWorkbenchTest::routesRemoteLoopbackConnectionPage() {
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller, true};
  controller.publishCurrentState();
  window.show();

  auto *tabs = window.findChild<QTabWidget *>("workbenchTabs");
  QVERIFY(tabs != nullptr);
  QCOMPARE(tab_index_named(tabs, QStringLiteral("Lab / LOOPBACK")), 4);
  tabs->setCurrentIndex(tab_index_named(tabs, QStringLiteral("Lab / LOOPBACK")));
  QCoreApplication::processEvents();

  auto *banner = window.findChild<QLabel *>("remoteConnectionBanner");
  auto *mode = window.findChild<QLabel *>("remoteModeValue");
  auto *session = window.findChild<QLabel *>("remoteSessionValue");
  auto *select_remote =
      window.findChild<QPushButton *>("remoteSelectLoopbackButton");
  auto *connect = window.findChild<QPushButton *>("remoteConnectButton");
  auto *disconnect = window.findChild<QPushButton *>("remoteDisconnectButton");
  auto *heartbeat = window.findChild<QLabel *>("remoteHeartbeatValue");
  auto *status_count = window.findChild<QLabel *>("remoteStatusCountValue");
  auto *overview_backend =
      window.findChild<QLabel *>("backendEvidenceValue");
  QVERIFY(banner != nullptr);
  QVERIFY(mode != nullptr);
  QVERIFY(session != nullptr);
  QVERIFY(select_remote != nullptr);
  QVERIFY(connect != nullptr);
  QVERIFY(disconnect != nullptr);
  QVERIFY(heartbeat != nullptr);
  QVERIFY(status_count != nullptr);
  QVERIFY(overview_backend != nullptr);

  QCOMPARE(mode->text(), QStringLiteral("LOCAL"));
  QVERIFY(banner->text().contains(QStringLiteral("LOCAL")));
  QVERIFY(!connect->isEnabled());

  select_remote->click();
  QTRY_COMPARE(mode->text(), QStringLiteral("REMOTE_LOOPBACK"));
  QVERIFY(banner->text().contains(QStringLiteral("DISCONNECTED")));
  QVERIFY(connect->isEnabled());

  connect->click();
  QTRY_COMPARE(session->text(), QStringLiteral("ESTABLISHED"));
  QVERIFY(banner->text().contains(QStringLiteral("LOOPBACK")));
  QVERIFY(banner->text().contains(QStringLiteral("NO PHYSICAL PC-ARM")));
  QTRY_VERIFY(status_count->text().toULongLong() >= 1);
  QTRY_VERIFY(heartbeat->text().contains(QStringLiteral("ok")));
  QVERIFY(disconnect->isEnabled());

  // Overview 仍走 Local adapter 证据，不被 Remote Connection 改写成 LOOPBACK。
  QCOMPARE(overview_backend->text(), QStringLiteral("SOCKETCAN / VCAN"));

  disconnect->click();
  QTRY_COMPARE(session->text(), QStringLiteral("WAITING_HELLO"));
  QVERIFY(banner->text().contains(QStringLiteral("DISCONNECTED")));
}

void QtWorkbenchTest::restoresHealthButtonsAfterWorkerCompletion() {
  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};
  controller.publishCurrentState();

  auto *run = window.findChild<QPushButton *>("runHealthButton");
  auto *cancel = window.findChild<QPushButton *>("cancelHealthButton");
  QVERIFY(run != nullptr);
  QVERIFY(cancel != nullptr);
  QSignalSpy completed{&controller, &WorkbenchController::healthCompleted};

  controller.startHealth();
  QVERIFY(!run->isEnabled());
  QVERIFY(cancel->isEnabled());
  QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
  QVERIFY(run->isEnabled());
  QVERIFY(!cancel->isEnabled());
}

void QtWorkbenchTest::runsCellPeerCanHealthAgainstCel1() {
  class Handler final : public rcr::workbench::CellAppHandler {
  public:
    rcr::workbench::CellAppStatus status() override {
      rcr::workbench::CellAppStatus out;
      out.started = true;
      out.online = true;
      out.mode = rcr::workbench::RuntimeModeCode::Idle;
      out.evidence = rcr::workbench::EvidenceClass::Physical;
      out.node_id = 1;
      out.session_id = 1;
      out.last_heartbeat_sequence = ++heartbeat_sequence_;
      out.heartbeat_age_ns = 15'000'000;
      return out;
    }
    rcr::workbench::CommandReply activate() override {
      return {rcr::workbench::CommandStatus::Accepted,
              rcr::workbench::RuntimeModeCode::Idle,
              rcr::workbench::RuntimeModeCode::Active, "ok"};
    }
    rcr::workbench::CommandReply
    submit_output(const rcr::workbench::DigitalOutputRequest &) override {
      return {rcr::workbench::CommandStatus::Accepted,
              rcr::workbench::RuntimeModeCode::Active,
              rcr::workbench::RuntimeModeCode::Active, "ok"};
    }

  private:
    std::uint16_t heartbeat_sequence_{0};
  };

  Handler handler;
  rcr::workbench::CellAppServer server{handler};
  QVERIFY(server.listen("127.0.0.1", 0));
  std::atomic<bool> run{true};
  std::thread worker([&] {
    while (run.load()) {
      static_cast<void>(server.poll(std::chrono::milliseconds{20}));
    }
  });
  struct Join {
    std::atomic<bool> *run;
    std::thread *worker;
    ~Join() {
      run->store(false);
      if (worker->joinable()) {
        worker->join();
      }
    }
  } guard{&run, &worker};

  rcr::workbench::CellAppClient client;
  QVERIFY(client
              .connect("127.0.0.1", server.port(),
                       std::chrono::milliseconds{500})
              .ok());
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{client, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};
  controller.publishCurrentState();

  QSignalSpy completed{&controller, &WorkbenchController::healthCompleted};
  controller.startHealth();
  QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 2500);
  const auto arguments = completed.takeFirst();
  const auto result =
      qvariant_cast<rcr::workbench::TestResult>(arguments.at(0));
  QCOMPARE(result.outcome, rcr::workbench::TestOutcome::Passed);
}

void QtWorkbenchTest::routesPhysicalModbusProbeThroughWorker() {
  rcr::workbench::PhysicalModbusIoService service{{}, mock_mr0};
  rcr::workbench::ModbusAgentServer server{service};
  QVERIFY(server.listen("127.0.0.1", 0));
  const auto port = server.port();
  std::thread agent([&] {
    static_cast<void>(server.serve_one(std::chrono::milliseconds{3000}));
  });
  struct JoinGuard {
    std::thread &t;
    ~JoinGuard() {
      if (t.joinable()) {
        t.join();
      }
    }
  } join_guard{agent};

  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};
  controller.publishCurrentState();
  window.show();

  auto *banner = window.findChild<QLabel *>("modbusMockBanner");
  auto *physical = window.findChild<QPushButton *>("modbusSelectPhysicalButton");
  auto *peer = window.findChild<QLineEdit *>("modbusAgentPeerEdit");
  auto *scan = window.findChild<QPushButton *>("modbusScanButton");
  auto *status = window.findChild<QLabel *>("modbusDeviceStatus");
  QVERIFY(banner != nullptr);
  QVERIFY(physical != nullptr);
  QVERIFY(peer != nullptr);
  QVERIFY(scan != nullptr);
  QVERIFY(status != nullptr);

  physical->click();
  QTRY_VERIFY(banner->text().contains(QStringLiteral("PHYSICAL MODBUS RTU")));
  QVERIFY(!status->text().contains(QStringLiteral("MOCK")));

  peer->setText(QStringLiteral("127.0.0.1:%1").arg(port));
  controller.setModbusAgentPeer(peer->text());
  scan->click();
  QTRY_VERIFY_WITH_TIMEOUT(status->text() == QStringLiteral("ONLINE"), 2000);
  QVERIFY(banner->text().contains(QStringLiteral("PHYSICAL")));
}

void QtWorkbenchTest::routesPhysicalModbusDiPollAndDoConfirm() {
  int fc02 = 0;
  rcr::workbench::PhysicalModbusIoService service{
      {}, [&](std::span<const std::uint8_t> request,
              std::chrono::milliseconds timeout)
          -> rcr::Result<std::vector<std::uint8_t>> {
        if (request.size() >= 2 &&
            request[1] == rcr::workbench::kModbusFnReadDiscreteInputs) {
          ++fc02;
          if (fc02 > 1) {
            return rtu_with_crc({0x01, 0x02, 0x01, 0x01});
          }
          return kLiveFc02Off;
        }
        return mock_mr0(request, timeout);
      }};
  rcr::workbench::ModbusAgentServer server{service};
  QVERIFY(server.listen("127.0.0.1", 0));
  const auto port = server.port();
  std::thread agent([&] {
    static_cast<void>(server.serve_one(std::chrono::milliseconds{5000}));
  });
  struct JoinGuard {
    std::thread &t;
    ~JoinGuard() {
      if (t.joinable()) {
        t.join();
      }
    }
  } join_guard{agent};

  rcr::DaemonConfig daemon_config{};
  daemon_config.can_if = "vcan-test";
  rcr::RuntimeDaemon daemon{daemon_config};
  rcr::workbench::RuntimeApplicationAdapter adapter{
      daemon, {rcr::workbench::EvidenceClass::Vcan, "SOCKETCAN"}};
  QTemporaryDir results;
  QVERIFY(results.isValid());
  WorkbenchController controller{adapter, provenance(),
                                 results.path().toStdString()};
  MainWindow window{controller};
  window.show();

  auto *physical = window.findChild<QPushButton *>("modbusSelectPhysicalButton");
  auto *peer = window.findChild<QLineEdit *>("modbusAgentPeerEdit");
  auto *scan = window.findChild<QPushButton *>("modbusScanButton");
  auto *status = window.findChild<QLabel *>("modbusDeviceStatus");
  auto *di0 = window.findChild<QLabel *>("modbusDi0Value");
  auto *do_request = window.findChild<QCheckBox *>("modbusDo1Request");
  auto *do_requested = window.findChild<QLabel *>("modbusDo1Requested");
  auto *do_confirmed = window.findChild<QLabel *>("modbusDo1Confirmed");
  QVERIFY(physical != nullptr);
  QVERIFY(peer != nullptr);
  QVERIFY(scan != nullptr);
  QVERIFY(status != nullptr);
  QVERIFY(di0 != nullptr);
  QVERIFY(do_request != nullptr);
  QVERIFY(do_requested != nullptr);
  QVERIFY(do_confirmed != nullptr);

  physical->click();
  peer->setText(QStringLiteral("127.0.0.1:%1").arg(port));
  controller.setModbusAgentPeer(peer->text());
  scan->click();
  QTRY_VERIFY_WITH_TIMEOUT(status->text() == QStringLiteral("ONLINE"), 2000);
  QTRY_VERIFY_WITH_TIMEOUT(di0->text() == QStringLiteral("● ON"), 2000);

  QTRY_VERIFY(do_request->isEnabled());
  if (!do_request->isChecked()) {
    do_request->click();
  }
  QTRY_COMPARE_WITH_TIMEOUT(do_requested->text(), QStringLiteral("ON"), 2000);
  QTRY_COMPARE_WITH_TIMEOUT(do_confirmed->text(), QStringLiteral("ON"), 2000);
}

QTEST_MAIN(QtWorkbenchTest)
#include "test_qt_workbench.moc"
