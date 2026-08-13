// 用 C++ 手写 Widgets，不用 Qt Designer（没有 .ui）也不用 QML。
// 六个页只是 Tab；按钮 clicked 转给 Controller，收到的 snapshot/TestResult
// 只填表。 这里没有 if (heartbeat_age > threshold) FAIL——那是 headless Health
// Test 的事。

#include "ui/main_window.hpp"

#include "controller/workbench_controller.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
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
  tabs->setObjectName(QStringLiteral("workbenchTabs"));
  tabs->addTab(makeOverviewPage(), QStringLiteral("Overview"));
  tabs->addTab(makeActuatorPage(), QStringLiteral("Actuator 01 (MOCK)"));
  tabs->addTab(makeModbusPage(), QStringLiteral("Modbus I/O (MOCK)"));
  tabs->addTab(makeTestsPage(), QStringLiteral("Tests"));
  tabs->addTab(makeDiagnosticsPage(), QStringLiteral("Diagnostics"));
  tabs->addTab(makeResultsPage(), QStringLiteral("Results"));
  setCentralWidget(tabs);

  // Window 和 Controller 都在 UI 线程，默认
  // DirectConnection：信号发出就立刻改控件。 和 worker 之间的排队连接写在
  // Controller 里，不写在这里。
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
  connect(&controller_, &WorkbenchController::modbusSnapshotReady, this,
          &MainWindow::updateModbus);
  connect(&controller_, &WorkbenchController::modbusCommandCompleted, this,
          &MainWindow::showModbusReply);
}

QWidget *MainWindow::makeOverviewPage() {
  auto *page = new QWidget(this);
  auto *form = new QFormLayout(page);
  runtime_state_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  runtime_state_->setObjectName(QStringLiteral("runtimeStateValue"));
  runtime_fault_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  interlock_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  backend_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  backend_->setObjectName(QStringLiteral("backendEvidenceValue"));
  interface_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  scheduler_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  device_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  heartbeat_ = new QLabel(QStringLiteral("N/A"), page);
  can_traffic_ = new QLabel(QStringLiteral("RX 0 / TX 0"), page);
  can_rejects_ =
      new QLabel(QStringLiteral("decode 0 / queue 0 / drop 0"), page);
  device_session_ = new QLabel(QStringLiteral("boot 0 / session 0"), page);
  output_ack_ = new QLabel(QStringLiteral("IDLE"), page);
  form->addRow(QStringLiteral("Runtime"), runtime_state_);
  form->addRow(QStringLiteral("Runtime fault"), runtime_fault_);
  form->addRow(QStringLiteral("Interlock"), interlock_);
  form->addRow(QStringLiteral("Backend / Evidence"), backend_);
  form->addRow(QStringLiteral("CAN interface"), interface_);
  form->addRow(QStringLiteral("Scheduler"), scheduler_);
  form->addRow(QStringLiteral("Device"), device_);
  form->addRow(QStringLiteral("Heartbeat age"), heartbeat_);
  form->addRow(QStringLiteral("CAN frames"), can_traffic_);
  form->addRow(QStringLiteral("Rejects / Drops"), can_rejects_);
  form->addRow(QStringLiteral("Boot / Session"), device_session_);
  form->addRow(QStringLiteral("Output ACK"), output_ack_);
  return page;
}

QWidget *MainWindow::makeTestsPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  run_health_ = new QPushButton(QStringLiteral("Run CAN Health"), page);
  run_health_->setObjectName(QStringLiteral("runHealthButton"));
  cancel_health_ = new QPushButton(QStringLiteral("Cancel"), page);
  cancel_health_->setObjectName(QStringLiteral("cancelHealthButton"));
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
  // 按钮只发“请开始/请取消”。采样、判定、写文件都在 worker 里跑同一套 headless
  // 对象。
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
  result_paths_->setObjectName(QStringLiteral("resultPathsValue"));
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
  actuator_state_->setObjectName(QStringLiteral("actuatorStateValue"));
  actuator_mode_ = new QLabel(QStringLiteral("NONE"), page);
  actuator_enabled_ = new QLabel(QStringLiteral("NO"), page);
  actuator_homed_ = new QLabel(QStringLiteral("NO"), page);
  actuator_position_ = new QLabel(QStringLiteral("0.000 rad"), page);
  actuator_velocity_ = new QLabel(QStringLiteral("0.000 rad/s"), page);
  actuator_limits_ = new QLabel(QStringLiteral("[-2.800, +2.800] rad"), page);
  actuator_fault_ = new QLabel(QStringLiteral("NONE"), page);
  actuator_reply_ = new QLabel(QStringLiteral("No command"), page);
  actuator_reply_->setObjectName(QStringLiteral("actuatorReplyValue"));
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
  drive_enable_->setObjectName(QStringLiteral("driveEnableButton"));
  drive_disable_ = new QPushButton(QStringLiteral("Drive Disable"), page);
  drive_disable_->setObjectName(QStringLiteral("driveDisableButton"));
  home_actuator_ = new QPushButton(QStringLiteral("HOME (MOCK)"), page);
  home_actuator_->setObjectName(QStringLiteral("homeActuatorButton"));
  start_actuator_ = new QPushButton(QStringLiteral("Start Velocity"), page);
  start_actuator_->setObjectName(QStringLiteral("startActuatorButton"));
  normal_stop_actuator_ = new QPushButton(QStringLiteral("Normal Stop"), page);
  normal_stop_actuator_->setObjectName(QStringLiteral("normalStopButton"));
  quick_stop_actuator_ =
      new QPushButton(QStringLiteral("QUICK STOP (software)"), page);
  quick_stop_actuator_->setObjectName(QStringLiteral("quickStopButton"));
  reset_actuator_fault_ = new QPushButton(QStringLiteral("Reset Fault"), page);
  reset_actuator_fault_->setObjectName(QStringLiteral("resetFaultButton"));
  jog_negative_ = new QPushButton(QStringLiteral("JOG -"), page);
  jog_negative_->setObjectName(QStringLiteral("jogNegativeButton"));
  jog_positive_ = new QPushButton(QStringLiteral("JOG +"), page);
  jog_positive_->setObjectName(QStringLiteral("jogPositiveButton"));
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

  // 初始文本是 DISABLED，因此在首帧到达前也必须呈现同一组允许操作。
  drive_disable_->setEnabled(false);
  home_actuator_->setEnabled(false);
  start_actuator_->setEnabled(false);
  normal_stop_actuator_->setEnabled(false);
  quick_stop_actuator_->setEnabled(false);
  jog_negative_->setEnabled(false);
  jog_positive_->setEnabled(false);
  reset_actuator_fault_->setEnabled(false);
  actuator_velocity_input_->setEnabled(false);
  jog_velocity_input_->setEnabled(false);

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
  // Jog 用 pressed/released，不用 clicked：按住才动，松手必须停。只点一下会立刻
  // release。
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

QWidget *MainWindow::makeModbusPage() {
  auto *page = new QWidget(this);
  auto *page_layout = new QVBoxLayout(page);
  auto *scroll = new QScrollArea(page);
  scroll->setWidgetResizable(true);
  auto *content = new QWidget(scroll);
  auto *layout = new QVBoxLayout(content);
  scroll->setWidget(content);
  page_layout->addWidget(scroll);

  auto *mock_label = new QLabel(
      QStringLiteral(
          "MOCK / NO PHYSICAL RS485 — Modbus RTU hardware not connected"),
      page);
  mock_label->setObjectName(QStringLiteral("modbusMockBanner"));
  mock_label->setStyleSheet(
      QStringLiteral("font-weight: bold; color: #9a6700;"));
  layout->addWidget(mock_label);

  auto *connection_group =
      new QGroupBox(QStringLiteral("Connection / Device"), page);
  auto *connection = new QFormLayout(connection_group);
  modbus_backend_ = new QLabel(QStringLiteral("MOCK"), connection_group);
  modbus_transport_ =
      new QLabel(QStringLiteral("Modbus RTU (planned)"), connection_group);
  modbus_serial_port_ =
      new QLabel(QStringLiteral("NOT CONNECTED"), connection_group);
  modbus_baud_ =
      new QLabel(QStringLiteral("9600 (placeholder)"), connection_group);
  modbus_parity_ =
      new QLabel(QStringLiteral("None (placeholder)"), connection_group);
  modbus_slave_ = new QLabel(QStringLiteral("1"), connection_group);
  modbus_status_ = new QLabel(QStringLiteral("MOCK ONLINE"), connection_group);
  modbus_status_->setObjectName(QStringLiteral("modbusDeviceStatus"));
  connection->addRow(QStringLiteral("Backend"), modbus_backend_);
  connection->addRow(QStringLiteral("Transport"), modbus_transport_);
  connection->addRow(QStringLiteral("Serial Port"), modbus_serial_port_);
  connection->addRow(QStringLiteral("Baud Rate"), modbus_baud_);
  connection->addRow(QStringLiteral("Parity"), modbus_parity_);
  connection->addRow(QStringLiteral("Slave"), modbus_slave_);
  connection->addRow(QStringLiteral("Status"), modbus_status_);
  layout->addWidget(connection_group);

  auto *scan_group = new QGroupBox(QStringLiteral("Slave Scan"), page);
  auto *scan_layout = new QVBoxLayout(scan_group);
  modbus_scan_ =
      new QPushButton(QStringLiteral("Scan Slaves (MOCK)"), scan_group);
  modbus_scan_->setObjectName(QStringLiteral("modbusScanButton"));
  modbus_scan_summary_ = new QLabel(QStringLiteral("UNKNOWN"), scan_group);
  modbus_scan_summary_->setObjectName(QStringLiteral("modbusScanSummary"));
  modbus_scan_summary_->setWordWrap(true);
  scan_layout->addWidget(modbus_scan_);
  scan_layout->addWidget(modbus_scan_summary_);
  connect(modbus_scan_, &QPushButton::clicked, &controller_,
          &WorkbenchController::requestModbusScan);
  layout->addWidget(scan_group);

  auto *di_group = new QGroupBox(QStringLiteral("DI Monitor"), page);
  auto *di_layout = new QGridLayout(di_group);
  di_layout->addWidget(new QLabel(QStringLiteral("Channel"), di_group), 0, 0);
  di_layout->addWidget(new QLabel(QStringLiteral("Observed"), di_group), 0, 1);
  di_layout->addWidget(
      new QLabel(QStringLiteral("MOCK injection request"), di_group), 0, 2);
  for (std::size_t channel = 0; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    const int row = static_cast<int>(channel + 1);
    di_layout->addWidget(
        new QLabel(QStringLiteral("DI%1").arg(channel), di_group), row, 0);
    modbus_di_values_[channel] = new QLabel(QStringLiteral("○ OFF"), di_group);
    modbus_di_values_[channel]->setObjectName(
        QStringLiteral("modbusDi%1Value").arg(channel));
    modbus_di_injections_[channel] =
        new QCheckBox(QStringLiteral("Set ON"), di_group);
    modbus_di_injections_[channel]->setObjectName(
        QStringLiteral("modbusDi%1Inject").arg(channel));
    di_layout->addWidget(modbus_di_values_[channel], row, 1);
    di_layout->addWidget(modbus_di_injections_[channel], row, 2);
    connect(modbus_di_injections_[channel], &QCheckBox::stateChanged, this,
            [this, channel](int state) {
              controller_.setMockDigitalInput(static_cast<int>(channel),
                                              state == Qt::Checked);
            });
  }
  layout->addWidget(di_group);

  auto *do_group = new QGroupBox(QStringLiteral("DO Control"), page);
  auto *do_layout = new QGridLayout(do_group);
  do_layout->addWidget(new QLabel(QStringLiteral("Request"), do_group), 0, 0);
  do_layout->addWidget(new QLabel(QStringLiteral("Requested"), do_group), 0, 1);
  do_layout->addWidget(new QLabel(QStringLiteral("Confirmed"), do_group), 0, 2);
  do_layout->addWidget(new QLabel(QStringLiteral("Reply"), do_group), 0, 3);
  for (std::size_t channel = 0; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    const int row = static_cast<int>(channel + 1);
    modbus_do_requests_[channel] =
        new QCheckBox(QStringLiteral("DO%1").arg(channel), do_group);
    modbus_do_requests_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Request").arg(channel));
    modbus_do_requested_[channel] = new QLabel(QStringLiteral("OFF"), do_group);
    modbus_do_requested_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Requested").arg(channel));
    modbus_do_confirmed_[channel] = new QLabel(QStringLiteral("OFF"), do_group);
    modbus_do_confirmed_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Confirmed").arg(channel));
    modbus_do_status_[channel] = new QLabel(QStringLiteral("NONE"), do_group);
    modbus_do_status_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Status").arg(channel));
    do_layout->addWidget(modbus_do_requests_[channel], row, 0);
    do_layout->addWidget(modbus_do_requested_[channel], row, 1);
    do_layout->addWidget(modbus_do_confirmed_[channel], row, 2);
    do_layout->addWidget(modbus_do_status_[channel], row, 3);
    connect(modbus_do_requests_[channel], &QCheckBox::stateChanged, this,
            [this, channel](int state) {
              controller_.requestDigitalOutput(static_cast<int>(channel),
                                               state == Qt::Checked);
            });
  }
  modbus_all_off_ =
      new QPushButton(QStringLiteral("All Outputs OFF"), do_group);
  modbus_all_off_->setObjectName(QStringLiteral("modbusAllOffButton"));
  modbus_reply_ = new QLabel(QStringLiteral("No command"), do_group);
  modbus_reply_->setObjectName(QStringLiteral("modbusReplyValue"));
  modbus_reply_->setWordWrap(true);
  do_layout->addWidget(modbus_all_off_, 5, 0, 1, 2);
  do_layout->addWidget(modbus_reply_, 5, 2, 1, 2);
  connect(modbus_all_off_, &QPushButton::clicked, &controller_,
          &WorkbenchController::requestAllOutputsOff);
  layout->addWidget(do_group);
  layout->addStretch();
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
  runtime_fault_->setText(
      text(rcr::workbench::to_string(snapshot.runtime.fault)));
  interlock_->setText(snapshot.runtime.interlock_ready
                          ? QStringLiteral("READY")
                          : QStringLiteral("NOT READY"));
  interface_->setText(text(snapshot.communication.interface_name));
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
  can_traffic_->setText(QStringLiteral("RX %1 / TX %2")
                            .arg(snapshot.communication.frames_received)
                            .arg(snapshot.communication.frames_sent));
  can_rejects_->setText(
      QStringLiteral("decode %1 / queue %2 / drop %3")
          .arg(snapshot.communication.decode_rejects)
          .arg(snapshot.communication.queue_rejects)
          .arg(snapshot.communication.input_queue_drop_count));
  device_session_->setText(QStringLiteral("boot %1 / session %2")
                               .arg(snapshot.device.boot_id)
                               .arg(snapshot.device.session_id));
  if (snapshot.output.ack_pending) {
    output_ack_->setText(QStringLiteral("PENDING session %1 / seq %2")
                             .arg(snapshot.output.last_sent_session)
                             .arg(snapshot.output.last_sent_sequence));
  } else {
    output_ack_->setText(QStringLiteral("%1 session %2 / seq %3")
                             .arg(text(rcr::workbench::to_string(
                                 snapshot.output.last_ack_result)))
                             .arg(snapshot.output.last_ack_session)
                             .arg(snapshot.output.last_ack_sequence));
  }
}

void MainWindow::showHealthStarted() {
  // 灰掉 Run、点亮 Cancel：防止 UI 连点。Controller 里还有 health_running_
  // 互斥。
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
  // result 已在 worker 线程判定完。写文件失败是独立字符串，不改 outcome，也不升
  // Runtime fault。
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
  actuator_velocity_input_->setEnabled(ready);
  jog_negative_->setEnabled(ready);
  jog_positive_->setEnabled(ready);
  jog_velocity_input_->setEnabled(ready);
  normal_stop_actuator_->setEnabled(moving);
  quick_stop_actuator_->setEnabled(moving);
  reset_actuator_fault_->setEnabled(faulted);
}

void MainWindow::showActuatorReply(
    const rcr::workbench::ActuatorCommandReply &reply) {
  actuator_reply_->setText(text(rcr::workbench::to_string(reply.status)) +
                           QStringLiteral(": ") + text(reply.message));
}

void MainWindow::updateModbus(
    const rcr::workbench::ModbusIoSnapshot &snapshot) {
  modbus_backend_->setText(text(snapshot.backend));
  modbus_transport_->setText(text(snapshot.transport));
  modbus_serial_port_->setText(text(snapshot.serial_port));
  modbus_baud_->setText(
      QStringLiteral("%1 (placeholder)").arg(snapshot.baud_rate_placeholder));
  modbus_parity_->setText(text(snapshot.parity_placeholder));
  modbus_slave_->setText(QString::number(snapshot.slave_id));
  modbus_status_->setText(QStringLiteral("MOCK %1").arg(
      text(rcr::workbench::to_string(snapshot.device_state))));

  QString scan = text(rcr::workbench::to_string(snapshot.scan_state));
  for (const auto &slave : snapshot.slaves) {
    scan += QStringLiteral("\nSlave %1  %2")
                .arg(slave.slave_id)
                .arg(text(rcr::workbench::to_string(slave.state)));
  }
  if (!snapshot.last_error.empty()) {
    scan += QStringLiteral("\n") + text(snapshot.last_error);
  }
  modbus_scan_summary_->setText(scan);
  modbus_scan_->setEnabled(snapshot.scan_state !=
                           rcr::workbench::ModbusScanState::Scanning);

  for (std::size_t channel = 0; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    const auto &input = snapshot.digital_inputs[channel];
    modbus_di_values_[channel]->setText(input.active ? QStringLiteral("● ON")
                                                     : QStringLiteral("○ OFF"));
    const QSignalBlocker input_blocker{modbus_di_injections_[channel]};
    modbus_di_injections_[channel]->setChecked(input.active);

    const auto &output = snapshot.digital_outputs[channel];
    const QSignalBlocker output_blocker{modbus_do_requests_[channel]};
    modbus_do_requests_[channel]->setChecked(output.requested);
    modbus_do_requested_[channel]->setText(
        output.requested ? QStringLiteral("ON") : QStringLiteral("OFF"));
    modbus_do_confirmed_[channel]->setText(
        output.confirmed ? QStringLiteral("ON") : QStringLiteral("OFF"));
    modbus_do_status_[channel]->setText(
        text(rcr::workbench::to_string(output.last_status)));
  }
}

void MainWindow::showModbusReply(
    const rcr::workbench::ModbusIoCommandReply &reply) {
  modbus_reply_->setText(text(rcr::workbench::to_string(reply.status)) +
                         QStringLiteral(": ") + text(reply.message));
}
