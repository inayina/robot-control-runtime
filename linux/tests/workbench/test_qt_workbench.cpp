#include "controller/workbench_controller.hpp"
#include "ui/main_window.hpp"

#include "rcr/runtime_daemon.hpp"
#include "rcr/workbench/application/runtime_application_adapter.hpp"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include <string>

namespace {

rcr::workbench::TestRunProvenance provenance() {
  return {"qt-test", true, "Debug"};
}

} // namespace

class QtWorkbenchTest final : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void rendersExplicitEvidence();
  void synchronizesInitialControls();
  void routesMockCommandsAndJogRelease();
  void restoresHealthButtonsAfterWorkerCompletion();
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
  MainWindow window{controller};
  controller.publishCurrentState();
  window.show();

  auto *tabs = window.findChild<QTabWidget *>("workbenchTabs");
  QVERIFY(tabs != nullptr);
  tabs->setCurrentIndex(1);
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

QTEST_MAIN(QtWorkbenchTest)
#include "test_qt_workbench.moc"
