// 用 C++ 手写 Widgets，不用 Qt Designer（没有 .ui）也不用 QML。
// 五个页只是 Tab；按钮 clicked 转给 Controller，收到的 snapshot/TestResult 只填表。
// 这里没有 if (heartbeat_age > threshold) FAIL——那是 headless Health Test 的事。

#include "ui/main_window.hpp"

#include "controller/workbench_controller.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace {

// 核心 DTO 用 std::string；QString 只出现在 UI 边界，避免 Runtime 头依赖 Qt。
QString text(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString text(const std::string &value) { return QString::fromStdString(value); }

QTableWidgetItem *item(const QString &value) {
  return new QTableWidgetItem(value);
}

} // namespace

MainWindow::MainWindow(WorkbenchController &controller, QWidget *parent)
    : QMainWindow(parent), controller_(controller) {
  setWindowTitle(QStringLiteral("Robot Device Test & Diagnostic Workbench"));
  resize(920, 620);

  auto *tabs = new QTabWidget(this);
  tabs->addTab(makeOverviewPage(), QStringLiteral("Overview"));
  tabs->addTab(makeActuatorPage(), QStringLiteral("Actuator 01 (MOCK)"));
  tabs->addTab(makeTestsPage(), QStringLiteral("Tests"));
  tabs->addTab(makeDiagnosticsPage(), QStringLiteral("Diagnostics"));
  tabs->addTab(makeResultsPage(), QStringLiteral("Results"));
  setCentralWidget(tabs);

  // Window 和 Controller 都在 UI 线程，默认 DirectConnection：信号发出就立刻改控件。
  // 和 worker 之间的排队连接写在 Controller 里，不写在这里。
  connect(&controller_, &WorkbenchController::snapshotReady, this,
          &MainWindow::updateSnapshot);
  connect(&controller_, &WorkbenchController::healthStarted, this,
          &MainWindow::showHealthStarted);
  connect(&controller_, &WorkbenchController::healthCompleted, this,
          &MainWindow::showHealthResult);
  connect(&controller_, &WorkbenchController::actuatorSnapshotReady, this,
          &MainWindow::updateActuator);
  connect(&controller_, &WorkbenchController::actuatorCommandCompleted, this,
          &MainWindow::showActuatorReply);
}

QWidget *MainWindow::makeOverviewPage() {
  auto *page = new QWidget(this);
  auto *form = new QFormLayout(page);
  runtime_state_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  backend_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  scheduler_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  device_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  heartbeat_ = new QLabel(QStringLiteral("N/A"), page);
  form->addRow(QStringLiteral("Runtime"), runtime_state_);
  form->addRow(QStringLiteral("Backend / Evidence"), backend_);
  form->addRow(QStringLiteral("Scheduler"), scheduler_);
  form->addRow(QStringLiteral("Device"), device_);
  form->addRow(QStringLiteral("Heartbeat age"), heartbeat_);
  return page;
}

QWidget *MainWindow::makeTestsPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  run_health_ = new QPushButton(QStringLiteral("Run CAN Health"), page);
  cancel_health_ = new QPushButton(QStringLiteral("Cancel"), page);
  cancel_health_->setEnabled(false);
  test_outcome_ = new QLabel(QStringLiteral("NOT RUN"), page);
  criteria_ = new QTableWidget(0, 4, page);
  criteria_->setHorizontalHeaderLabels(
      {QStringLiteral("Criterion"), QStringLiteral("Pass"),
       QStringLiteral("Expected"), QStringLiteral("Actual")});
  criteria_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(run_health_);
  layout->addWidget(cancel_health_);
  layout->addWidget(test_outcome_);
  layout->addWidget(criteria_);
  // 按钮只发“请开始/请取消”。采样、判定、写文件都在 worker 里跑同一套 headless 对象。
  connect(run_health_, &QPushButton::clicked, &controller_,
          &WorkbenchController::startHealth);
  connect(cancel_health_, &QPushButton::clicked, &controller_,
          &WorkbenchController::cancelHealth);
  return page;
}

QWidget *MainWindow::makeDiagnosticsPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  diagnostics_ = new QTableWidget(0, 5, page);
  diagnostics_->setHorizontalHeaderLabels(
      {QStringLiteral("Source"), QStringLiteral("Severity"),
       QStringLiteral("Code"), QStringLiteral("Message"),
       QStringLiteral("Device")});
  diagnostics_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(diagnostics_);
  return page;
}

QWidget *MainWindow::makeResultsPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  result_paths_ = new QLabel(QStringLiteral("No result artifacts"), page);
  // 路径要能鼠标选中复制；UI 自己不负责打开或校验这些文件。
  result_paths_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  result_paths_->setWordWrap(true);
  layout->addWidget(result_paths_);
  layout->addStretch();
  return page;
}

QWidget *MainWindow::makeActuatorPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  // 横幅写死 MOCK：这页推进的是进程内仿真，不发运动 CAN，不能当实物验收。
  auto *mock_label = new QLabel(
      QStringLiteral(
          "MOCK / ISOLATED — no physical actuator or CAN motion command"),
      page);
  mock_label->setStyleSheet(
      QStringLiteral("font-weight: bold; color: #9a6700;"));
  layout->addWidget(mock_label);

  auto *form = new QFormLayout();
  actuator_state_ = new QLabel(QStringLiteral("DISABLED"), page);
  actuator_mode_ = new QLabel(QStringLiteral("NONE"), page);
  actuator_enabled_ = new QLabel(QStringLiteral("NO"), page);
  actuator_homed_ = new QLabel(QStringLiteral("NO"), page);
  actuator_position_ = new QLabel(QStringLiteral("0.000 rad"), page);
  actuator_velocity_ = new QLabel(QStringLiteral("0.000 rad/s"), page);
  actuator_limits_ = new QLabel(QStringLiteral("[-2.800, +2.800] rad"), page);
  actuator_fault_ = new QLabel(QStringLiteral("NONE"), page);
  actuator_reply_ = new QLabel(QStringLiteral("No command"), page);
  actuator_reply_->setWordWrap(true);
  form->addRow(QStringLiteral("State"), actuator_state_);
  form->addRow(QStringLiteral("Motion mode"), actuator_mode_);
  form->addRow(QStringLiteral("Drive enabled"), actuator_enabled_);
  form->addRow(QStringLiteral("Homed"), actuator_homed_);
  form->addRow(QStringLiteral("Position target / actual / error"),
               actuator_position_);
  form->addRow(QStringLiteral("Velocity target / actual / error"),
               actuator_velocity_);
  form->addRow(QStringLiteral("Soft limits"), actuator_limits_);
  form->addRow(QStringLiteral("Fault / reject"), actuator_fault_);
  form->addRow(QStringLiteral("Last command"), actuator_reply_);
  layout->addLayout(form);

  auto *controls = new QGridLayout();
  drive_enable_ = new QPushButton(QStringLiteral("DRIVE ENABLE"), page);
  drive_disable_ = new QPushButton(QStringLiteral("Drive Disable"), page);
  home_actuator_ = new QPushButton(QStringLiteral("HOME (MOCK)"), page);
  start_actuator_ = new QPushButton(QStringLiteral("Start Velocity"), page);
  normal_stop_actuator_ = new QPushButton(QStringLiteral("Normal Stop"), page);
  quick_stop_actuator_ =
      new QPushButton(QStringLiteral("QUICK STOP (software)"), page);
  reset_actuator_fault_ = new QPushButton(QStringLiteral("Reset Fault"), page);
  jog_negative_ = new QPushButton(QStringLiteral("JOG -"), page);
  jog_positive_ = new QPushButton(QStringLiteral("JOG +"), page);
  actuator_velocity_input_ = new QDoubleSpinBox(page);
  actuator_velocity_input_->setRange(-2.0, 2.0);
  actuator_velocity_input_->setSingleStep(0.1);
  actuator_velocity_input_->setValue(1.0);
  actuator_velocity_input_->setSuffix(QStringLiteral(" rad/s"));
  jog_velocity_input_ = new QDoubleSpinBox(page);
  jog_velocity_input_->setRange(0.1, 2.0);
  jog_velocity_input_->setSingleStep(0.1);
  jog_velocity_input_->setValue(0.5);
  jog_velocity_input_->setSuffix(QStringLiteral(" rad/s"));

  controls->addWidget(drive_enable_, 0, 0);
  controls->addWidget(drive_disable_, 0, 1);
  controls->addWidget(home_actuator_, 0, 2);
  controls->addWidget(actuator_velocity_input_, 1, 0);
  controls->addWidget(start_actuator_, 1, 1);
  controls->addWidget(normal_stop_actuator_, 1, 2);
  controls->addWidget(jog_velocity_input_, 2, 0);
  controls->addWidget(jog_negative_, 2, 1);
  controls->addWidget(jog_positive_, 2, 2);
  controls->addWidget(quick_stop_actuator_, 3, 0, 1, 2);
  controls->addWidget(reset_actuator_fault_, 3, 2);
  layout->addLayout(controls);
  layout->addStretch();

  connect(drive_enable_, &QPushButton::clicked, &controller_,
          &WorkbenchController::driveEnable);
  connect(drive_disable_, &QPushButton::clicked, &controller_,
          &WorkbenchController::driveDisable);
  connect(home_actuator_, &QPushButton::clicked, &controller_,
          &WorkbenchController::homeActuator);
  connect(start_actuator_, &QPushButton::clicked, this, [this] {
    controller_.startActuatorVelocity(actuator_velocity_input_->value());
  });
  connect(normal_stop_actuator_, &QPushButton::clicked, &controller_,
          &WorkbenchController::normalStopActuator);
  connect(quick_stop_actuator_, &QPushButton::clicked, &controller_,
          &WorkbenchController::quickStopActuator);
  connect(reset_actuator_fault_, &QPushButton::clicked, &controller_,
          &WorkbenchController::resetActuatorFault);
  // Jog 用 pressed/released，不用 clicked：按住才动，松手必须停。只点一下会立刻 release。
  connect(jog_negative_, &QPushButton::pressed, this,
          [this] { controller_.jogPressed(-1, jog_velocity_input_->value()); });
  connect(jog_positive_, &QPushButton::pressed, this,
          [this] { controller_.jogPressed(1, jog_velocity_input_->value()); });
  connect(jog_negative_, &QPushButton::released, &controller_,
          &WorkbenchController::jogReleased);
  connect(jog_positive_, &QPushButton::released, &controller_,
          &WorkbenchController::jogReleased);
  return page;
}

void MainWindow::updateSnapshot(
    const rcr::workbench::RuntimeTelemetrySnapshot &snapshot) {
  // 只格式化已经算好的快照。heartbeat 超龄会不会 FAIL，不在这里判断。
  runtime_state_->setText(
      text(rcr::workbench::to_string(snapshot.runtime.mode)));
  backend_->setText(
      text(snapshot.communication.backend) + QStringLiteral(" / ") +
      text(rcr::workbench::to_string(snapshot.communication.evidence)));
  scheduler_->setText(snapshot.runtime.scheduler_running
                          ? QStringLiteral("RUNNING")
                          : QStringLiteral("STOPPED"));
  device_->setText(text(snapshot.device.device_id) + QStringLiteral(" / ") +
                   (snapshot.device.online ? QStringLiteral("ONLINE")
                                           : QStringLiteral("OFFLINE")));
  heartbeat_->setText(snapshot.device.heartbeat_age_ns < 0
                          ? QStringLiteral("N/A")
                          : QStringLiteral("%1 ms").arg(
                                snapshot.device.heartbeat_age_ns / 1'000'000));
}

void MainWindow::showHealthStarted() {
  // 灰掉 Run、点亮 Cancel：防止 UI 连点。Controller 里还有 health_running_ 互斥。
  run_health_->setEnabled(false);
  cancel_health_->setEnabled(true);
  test_outcome_->setText(QStringLiteral("RUNNING"));
  criteria_->setRowCount(0);
  diagnostics_->setRowCount(0);
  result_paths_->setText(QStringLiteral("Result pending"));
}

void MainWindow::showHealthResult(const rcr::workbench::TestResult &result,
                                  const QString &json_path,
                                  const QString &csv_path,
                                  const QString &persistence_error) {
  // result 已在 worker 线程判定完。写文件失败是独立字符串，不改 outcome，也不升 Runtime fault。
  run_health_->setEnabled(true);
  cancel_health_->setEnabled(false);
  test_outcome_->setText(text(rcr::workbench::to_string(result.outcome)) +
                         QStringLiteral(": ") + text(result.reason));

  criteria_->setRowCount(static_cast<int>(result.criteria.size()));
  for (std::size_t index = 0; index < result.criteria.size(); ++index) {
    const auto &criterion = result.criteria[index];
    const int row = static_cast<int>(index);
    criteria_->setItem(row, 0, item(text(criterion.name)));
    criteria_->setItem(
        row, 1,
        item(criterion.passed ? QStringLiteral("YES") : QStringLiteral("NO")));
    criteria_->setItem(row, 2, item(text(criterion.expected)));
    criteria_->setItem(row, 3, item(text(criterion.actual)));
  }

  diagnostics_->setRowCount(static_cast<int>(result.diagnostics.size()));
  for (std::size_t index = 0; index < result.diagnostics.size(); ++index) {
    const auto &event = result.diagnostics[index];
    const int row = static_cast<int>(index);
    diagnostics_->setItem(row, 0,
                          item(text(rcr::workbench::to_string(event.source))));
    diagnostics_->setItem(
        row, 1, item(text(rcr::workbench::to_string(event.severity))));
    diagnostics_->setItem(row, 2, item(text(event.code)));
    diagnostics_->setItem(row, 3, item(text(event.message)));
    diagnostics_->setItem(row, 4, item(text(event.device_id)));
  }

  if (!persistence_error.isEmpty()) {
    result_paths_->setText(QStringLiteral("Persistence ERROR: ") +
                           persistence_error);
  } else {
    result_paths_->setText(
        QStringLiteral("JSON: %1\nCSV: %2").arg(json_path, csv_path));
  }
}

void MainWindow::updateActuator(
    const rcr::workbench::ActuatorSnapshot &snapshot) {
  actuator_state_->setText(text(rcr::workbench::to_string(snapshot.state)));
  actuator_mode_->setText(
      text(rcr::workbench::to_string(snapshot.motion_mode)));
  actuator_enabled_->setText(snapshot.drive_enabled ? QStringLiteral("YES")
                                                    : QStringLiteral("NO"));
  actuator_homed_->setText(snapshot.homed ? QStringLiteral("YES")
                                          : QStringLiteral("NO"));
  actuator_position_->setText(QStringLiteral("%1 / %2 / %3 rad")
                                  .arg(snapshot.target_position_rad, 0, 'f', 3)
                                  .arg(snapshot.actual_position_rad, 0, 'f', 3)
                                  .arg(snapshot.position_error_rad, 0, 'f', 3));
  actuator_velocity_->setText(
      QStringLiteral("%1 / %2 / %3 rad/s")
          .arg(snapshot.target_velocity_rad_s, 0, 'f', 3)
          .arg(snapshot.actual_velocity_rad_s, 0, 'f', 3)
          .arg(snapshot.velocity_error_rad_s, 0, 'f', 3));
  actuator_limits_->setText(QStringLiteral("[%1, %2] rad")
                                .arg(snapshot.min_position_rad, 0, 'f', 3)
                                .arg(snapshot.max_position_rad, 0, 'f', 3));
  QString fault = text(rcr::workbench::to_string(snapshot.fault));
  if (!snapshot.last_reject.empty()) {
    fault += QStringLiteral(" / ") + text(snapshot.last_reject);
  }
  actuator_fault_->setText(fault);

  // 按钮灰显只是按已发布状态做操作提示，不是第二套状态机；拒绝仍以 reply 为准。
  const bool disabled =
      snapshot.state == rcr::workbench::ActuatorState::Disabled;
  const bool ready = snapshot.state == rcr::workbench::ActuatorState::Ready;
  const bool idle = snapshot.state == rcr::workbench::ActuatorState::Idle;
  const bool moving =
      snapshot.state == rcr::workbench::ActuatorState::Running ||
      snapshot.state == rcr::workbench::ActuatorState::Homing ||
      snapshot.state == rcr::workbench::ActuatorState::Stopping;
  const bool faulted = snapshot.state == rcr::workbench::ActuatorState::Fault;
  drive_enable_->setEnabled(disabled);
  drive_disable_->setEnabled(!disabled && !faulted);
  home_actuator_->setEnabled(idle || ready);
  start_actuator_->setEnabled(ready);
  jog_negative_->setEnabled(ready);
  jog_positive_->setEnabled(ready);
  normal_stop_actuator_->setEnabled(moving);
  quick_stop_actuator_->setEnabled(moving);
  reset_actuator_fault_->setEnabled(faulted);
}

void MainWindow::showActuatorReply(
    const rcr::workbench::ActuatorCommandReply &reply) {
  actuator_reply_->setText(text(rcr::workbench::to_string(reply.status)) +
                           QStringLiteral(": ") + text(reply.message));
}
