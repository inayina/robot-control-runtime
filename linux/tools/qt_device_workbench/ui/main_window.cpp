// 用 C++ 手写 Widgets，不用 Qt Designer（没有 .ui）也不用 QML。
// 默认四页只是 Tab；Lab 页仍构造但默认不进导航。按钮 clicked 转给 Controller，
// 收到的 snapshot/TestResult 只填表。这里没有 if (heartbeat_age > threshold)
// FAIL——那是 headless Health Test 的事。

#include "ui/main_window.hpp"

#include "controller/workbench_controller.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
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

MainWindow::MainWindow(WorkbenchController &controller, bool show_lab,
                       QWidget *parent)
    : QMainWindow(parent), controller_(controller) {
  setWindowTitle(
      QStringLiteral("Robot Edge Runtime & Device Commissioning Workbench"));
  resize(920, 680);

  auto *tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("workbenchTabs"));
  tabs->addTab(makeOverviewPage(), QStringLiteral("Overview"));
  tabs->addTab(makeRuntimePage(), QStringLiteral("Runtime"));
  tabs->addTab(makeModbusPage(), QStringLiteral("Cell I/O"));
  tabs->addTab(makeVerificationPage(), QStringLiteral("Verification"));
  auto *loopback = makeConnectionPage();
  auto *actuator = makeActuatorPage();
  if (show_lab) {
    tabs->addTab(loopback, QStringLiteral("Lab / LOOPBACK"));
    tabs->addTab(actuator, QStringLiteral("Lab / Actuator MOCK"));
  } else {
    // 仍构造页面，测试和 --show-lab 能找到控件；默认不进顶层导航。
    loopback->setParent(tabs);
    actuator->setParent(tabs);
    loopback->hide();
    actuator->hide();
  }
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
  connect(&controller_, &WorkbenchController::remoteConnectionReady, this,
          &MainWindow::updateRemoteConnection);
  connect(&controller_, &WorkbenchController::runtimeCommandCompleted, this,
          &MainWindow::showRuntimeCommand);
}

QWidget *MainWindow::makeOverviewPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);

  auto *runtime_group = new QGroupBox(QStringLiteral("ROBOT RUNTIME"), page);
  auto *runtime_form = new QFormLayout(runtime_group);
  overview_runtime_ = new QLabel(QStringLiteral("UNKNOWN"), runtime_group);
  overview_runtime_->setObjectName(QStringLiteral("overviewRuntimeValue"));
  overview_fault_ = new QLabel(QStringLiteral("NONE"), runtime_group);
  overview_fault_->setObjectName(QStringLiteral("overviewFaultValue"));
  runtime_form->addRow(QStringLiteral("Runtime State"), overview_runtime_);
  runtime_form->addRow(QStringLiteral("Runtime Fault"), overview_fault_);
  layout->addWidget(runtime_group);

  auto *node_group = new QGroupBox(QStringLiteral("ROBOT NODE"), page);
  auto *node_form = new QFormLayout(node_group);
  overview_node_ = new QLabel(QStringLiteral("OFFLINE"), node_group);
  overview_node_->setObjectName(QStringLiteral("overviewNodeValue"));
  overview_position_reached_ = new QLabel(QStringLiteral("NOT REACHED"), node_group);
  overview_position_reached_->setObjectName(
      QStringLiteral("overviewPositionReachedValue"));
  overview_heartbeat_ = new QLabel(QStringLiteral("N/A"), node_group);
  overview_heartbeat_->setObjectName(QStringLiteral("overviewHeartbeatValue"));
  node_form->addRow(QStringLiteral("CAN Node"), overview_node_);
  node_form->addRow(QStringLiteral("Target Position"), overview_position_reached_);
  node_form->addRow(QStringLiteral("Heartbeat age"), overview_heartbeat_);
  layout->addWidget(node_group);

  auto *cell_group = new QGroupBox(QStringLiteral("CELL"), page);
  auto *cell_form = new QFormLayout(cell_group);
  overview_cell_ready_ = new QLabel(QStringLiteral("FALSE"), cell_group);
  overview_cell_ready_->setObjectName(QStringLiteral("overviewCellReadyValue"));
  overview_do0_requested_ = new QLabel(QStringLiteral("OFF"), cell_group);
  overview_do0_requested_->setObjectName(
      QStringLiteral("overviewDo0RequestedValue"));
  overview_do0_confirmed_ = new QLabel(QStringLiteral("OFF"), cell_group);
  overview_do0_confirmed_->setObjectName(
      QStringLiteral("overviewDo0ConfirmedValue"));
  overview_mr0_ = new QLabel(QStringLiteral("UNKNOWN"), cell_group);
  overview_mr0_->setObjectName(QStringLiteral("overviewMr0Value"));
  cell_form->addRow(QStringLiteral("Cell Ready"), overview_cell_ready_);
  cell_form->addRow(QStringLiteral("Cell Ready Output"), overview_do0_requested_);
  cell_form->addRow(QStringLiteral("Confirmed"), overview_do0_confirmed_);
  cell_form->addRow(QStringLiteral("MR0 connection"), overview_mr0_);
  layout->addWidget(cell_group);
  layout->addStretch();
  return page;
}

QWidget *MainWindow::makeRuntimePage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);

  auto *state_group = new QGroupBox(QStringLiteral("Runtime State"), page);
  auto *state_form = new QFormLayout(state_group);
  runtime_state_ = new QLabel(QStringLiteral("UNKNOWN"), state_group);
  runtime_state_->setObjectName(QStringLiteral("runtimeStateValue"));
  runtime_fault_ = new QLabel(QStringLiteral("UNKNOWN"), state_group);
  interlock_ = new QLabel(QStringLiteral("UNKNOWN"), state_group);
  scheduler_ = new QLabel(QStringLiteral("UNKNOWN"), state_group);
  backend_ = new QLabel(QStringLiteral("UNKNOWN"), state_group);
  backend_->setObjectName(QStringLiteral("backendEvidenceValue"));
  activate_runtime_ =
      new QPushButton(QStringLiteral("Activate Runtime"), state_group);
  activate_runtime_->setObjectName(QStringLiteral("activateRuntimeButton"));
  last_command_ = new QLabel(QStringLiteral("NONE"), state_group);
  last_command_->setObjectName(QStringLiteral("lastCommandReplyValue"));
  last_command_->setWordWrap(true);
  state_form->addRow(QStringLiteral("State"), runtime_state_);
  state_form->addRow(QStringLiteral("Fault"), runtime_fault_);
  state_form->addRow(QStringLiteral("Interlock"), interlock_);
  state_form->addRow(QStringLiteral("Scheduler"), scheduler_);
  state_form->addRow(QStringLiteral("Evidence"), backend_);
  state_form->addRow(activate_runtime_);
  state_form->addRow(QStringLiteral("Last command"), last_command_);
  layout->addWidget(state_group);

  auto *node_group = new QGroupBox(QStringLiteral("Robot Node"), page);
  auto *node_form = new QFormLayout(node_group);
  device_ = new QLabel(QStringLiteral("UNKNOWN"), node_group);
  node_sensor_ = new QLabel(QStringLiteral("NOT REACHED"), node_group);
  node_sensor_->setObjectName(QStringLiteral("runtimeTargetSensorValue"));
  node_sensor_->setToolTip(
      QStringLiteral("CAN NodeStatus.input_bits bit0 (PA0 TARGET_SENSOR_DO)"));
  interface_ = new QLabel(QStringLiteral("UNKNOWN"), node_group);
  heartbeat_ = new QLabel(QStringLiteral("N/A"), node_group);
  device_session_ = new QLabel(QStringLiteral("boot 0 / session 0"), node_group);
  can_traffic_ = new QLabel(QStringLiteral("RX 0 / TX 0"), node_group);
  can_rejects_ =
      new QLabel(QStringLiteral("decode 0 / queue 0 / drop 0"), node_group);
  node_form->addRow(QStringLiteral("Node"), device_);
  node_form->addRow(QStringLiteral("Target Sensor"), node_sensor_);
  node_form->addRow(QStringLiteral("CAN interface"), interface_);
  node_form->addRow(QStringLiteral("Heartbeat age"), heartbeat_);
  node_form->addRow(QStringLiteral("Boot / Session"), device_session_);
  node_form->addRow(QStringLiteral("RX / TX"), can_traffic_);
  node_form->addRow(QStringLiteral("Reject / Drop"), can_rejects_);
  layout->addWidget(node_group);

  auto *motion_group =
      new QGroupBox(QStringLiteral("Motion Commissioning"), page);
  auto *motion_layout = new QVBoxLayout(motion_group);
  auto *hint = new QLabel(
      QStringLiteral(
          "Activate enables output admission until Deactivate, E-stop, "
          "interlock loss, comm loss, or an in-flight command/ACK timeout. "
          "After CAN is replugged, Activate clears CommLoss (node must be "
          "online) then re-enables outputs. HOME / TARGET are node output "
          "commands, not Runtime state transitions."),
      motion_group);
  hint->setWordWrap(true);
  command_home_ = new QPushButton(QStringLiteral("Command HOME"), motion_group);
  command_home_->setObjectName(QStringLiteral("commandServoHomeButton"));
  command_target_ =
      new QPushButton(QStringLiteral("Command TARGET"), motion_group);
  command_target_->setObjectName(QStringLiteral("commandServoTargetButton"));
  output_ack_ = new QLabel(QStringLiteral("IDLE"), motion_group);
  output_ack_->setObjectName(QStringLiteral("outputAckValue"));
  motion_layout->addWidget(hint);
  motion_layout->addWidget(command_home_);
  motion_layout->addWidget(command_target_);
  auto *ack_form = new QFormLayout();
  ack_form->addRow(QStringLiteral("Output ACK"), output_ack_);
  motion_layout->addLayout(ack_form);
  layout->addWidget(motion_group);
  layout->addStretch();

  connect(activate_runtime_, &QPushButton::clicked, &controller_,
          &WorkbenchController::activateRuntime);
  connect(command_home_, &QPushButton::clicked, &controller_,
          &WorkbenchController::commandServoHome);
  connect(command_target_, &QPushButton::clicked, &controller_,
          &WorkbenchController::commandServoTarget);
  return page;
}

QWidget *MainWindow::makeVerificationPage() {
  auto *page = new QTabWidget(this);
  page->setObjectName(QStringLiteral("verificationTabs"));
  page->addTab(makeTestsPage(), QStringLiteral("Test"));
  page->addTab(makeDiagnosticsPage(), QStringLiteral("Events"));
  page->addTab(makeResultsPage(), QStringLiteral("Evidence"));
  return page;
}

QWidget *MainWindow::makeConnectionPage() {
  // 只展示 Connection 状态并发出 Local/Remote/Connect 请求；不创建 QTcpSocket，
  // 不做 framing/CRC/重连判定。
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);

  remote_banner_ = new QLabel(
      QStringLiteral("LOCAL / SAME PROCESS — Overview still uses in-process "
                     "adapter"),
      page);
  remote_banner_->setObjectName(QStringLiteral("remoteConnectionBanner"));
  remote_banner_->setWordWrap(true);
  remote_banner_->setStyleSheet(
      QStringLiteral("font-weight: bold; color: #9a6700;"));
  layout->addWidget(remote_banner_);

  auto *backend_group =
      new QGroupBox(QStringLiteral("Runtime Client Backend"), page);
  auto *backend_layout = new QHBoxLayout(backend_group);
  remote_select_local_ =
      new QPushButton(QStringLiteral("Use Local"), backend_group);
  remote_select_local_->setObjectName(QStringLiteral("remoteSelectLocalButton"));
  remote_select_loopback_ =
      new QPushButton(QStringLiteral("Use Remote LOOPBACK"), backend_group);
  remote_select_loopback_->setObjectName(
      QStringLiteral("remoteSelectLoopbackButton"));
  remote_connect_ =
      new QPushButton(QStringLiteral("Connect (HELLO)"), backend_group);
  remote_connect_->setObjectName(QStringLiteral("remoteConnectButton"));
  remote_disconnect_ =
      new QPushButton(QStringLiteral("Disconnect"), backend_group);
  remote_disconnect_->setObjectName(QStringLiteral("remoteDisconnectButton"));
  backend_layout->addWidget(remote_select_local_);
  backend_layout->addWidget(remote_select_loopback_);
  backend_layout->addWidget(remote_connect_);
  backend_layout->addWidget(remote_disconnect_);
  layout->addWidget(backend_group);

  auto *status_group = new QGroupBox(QStringLiteral("Connection Status"), page);
  auto *status = new QFormLayout(status_group);
  remote_mode_ = new QLabel(QStringLiteral("LOCAL"), status_group);
  remote_mode_->setObjectName(QStringLiteral("remoteModeValue"));
  remote_peer_ = new QLabel(QStringLiteral("n/a"), status_group);
  remote_peer_->setObjectName(QStringLiteral("remotePeerValue"));
  remote_session_ = new QLabel(QStringLiteral("WAITING_HELLO"), status_group);
  remote_session_->setObjectName(QStringLiteral("remoteSessionValue"));
  remote_heartbeat_ =
      new QLabel(QStringLiteral("ok 0 / missed 0"), status_group);
  remote_heartbeat_->setObjectName(QStringLiteral("remoteHeartbeatValue"));
  remote_status_count_ = new QLabel(QStringLiteral("0"), status_group);
  remote_status_count_->setObjectName(QStringLiteral("remoteStatusCountValue"));
  remote_status_mode_ = new QLabel(QStringLiteral("n/a"), status_group);
  remote_status_mode_->setObjectName(QStringLiteral("remoteStatusModeValue"));
  remote_last_error_ = new QLabel(QStringLiteral("-"), status_group);
  remote_last_error_->setObjectName(QStringLiteral("remoteLastErrorValue"));
  status->addRow(QStringLiteral("Mode"), remote_mode_);
  status->addRow(QStringLiteral("Peer"), remote_peer_);
  status->addRow(QStringLiteral("Session"), remote_session_);
  status->addRow(QStringLiteral("Heartbeat"), remote_heartbeat_);
  status->addRow(QStringLiteral("STATUS replies"), remote_status_count_);
  status->addRow(QStringLiteral("Remote status mode"), remote_status_mode_);
  status->addRow(QStringLiteral("Last error"), remote_last_error_);
  layout->addWidget(status_group);
  layout->addStretch(1);

  connect(remote_select_local_, &QPushButton::clicked, &controller_,
          &WorkbenchController::selectLocalBackend);
  connect(remote_select_loopback_, &QPushButton::clicked, &controller_,
          &WorkbenchController::selectRemoteLoopbackBackend);
  connect(remote_connect_, &QPushButton::clicked, &controller_,
          &WorkbenchController::connectRemoteLoopback);
  connect(remote_disconnect_, &QPushButton::clicked, &controller_,
          &WorkbenchController::disconnectRemoteLoopback);
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
  if (controller_.cellPeerMode()) {
    auto *hint = new QLabel(
        QStringLiteral(
            "Samples Orange Pi CEL1 snapshots (heartbeat / rejects). "
            "This process does not open SocketCAN."),
        page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
  }
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

  auto *status_group = new QGroupBox(QStringLiteral("CELL STATUS"), content);
  auto *status_form = new QFormLayout(status_group);
  cell_ready_value_ = new QLabel(QStringLiteral("FALSE"), status_group);
  cell_ready_value_->setObjectName(QStringLiteral("cellReadyValue"));
  cell_connection_value_ = new QLabel(QStringLiteral("UNKNOWN"), status_group);
  cell_connection_value_->setObjectName(QStringLiteral("cellConnectionValue"));
  status_form->addRow(QStringLiteral("Remote I/O"),
                      new QLabel(QStringLiteral("MR0-IOR08"), status_group));
  status_form->addRow(QStringLiteral("Connection"), cell_connection_value_);
  status_form->addRow(QStringLiteral("Cell Ready"), cell_ready_value_);
  layout->addWidget(status_group);

  auto *output_group =
      new QGroupBox(QStringLiteral("CELL READY OUTPUT"), content);
  auto *output_form = new QFormLayout(output_group);
  cell_output_owner_ = new QLabel(
      controller_.cellPeerMode() ? QStringLiteral("AUTO / EDGE")
                                 : QStringLiteral("AUTO / CellReadyMapper"),
      output_group);
  cell_output_owner_->setObjectName(QStringLiteral("cellReadyOutputOwner"));
  modbus_do_requests_[0] =
      new QCheckBox(QStringLiteral("DO0 AUTO / Cell Ready"), output_group);
  modbus_do_requests_[0]->setObjectName(QStringLiteral("modbusDo0Request"));
  modbus_do_requests_[0]->setEnabled(false);
  modbus_do_requested_[0] = new QLabel(QStringLiteral("OFF"), output_group);
  modbus_do_requested_[0]->setObjectName(QStringLiteral("modbusDo0Requested"));
  modbus_do_confirmed_[0] = new QLabel(QStringLiteral("OFF"), output_group);
  modbus_do_confirmed_[0]->setObjectName(QStringLiteral("modbusDo0Confirmed"));
  modbus_do_status_[0] = new QLabel(QStringLiteral("NONE"), output_group);
  modbus_do_status_[0]->setObjectName(QStringLiteral("modbusDo0Status"));
  cell_output_status_ = modbus_do_status_[0];
  output_form->addRow(QStringLiteral("Channel"),
                      new QLabel(QStringLiteral("MR0-IOR08 DO0"), output_group));
  output_form->addRow(QStringLiteral("Owner"), cell_output_owner_);
  output_form->addRow(QStringLiteral("Requested"), modbus_do_requested_[0]);
  output_form->addRow(QStringLiteral("Confirmed"), modbus_do_confirmed_[0]);
  output_form->addRow(QStringLiteral("Reply"), modbus_do_status_[0]);
  output_form->addRow(modbus_do_requests_[0]);
  layout->addWidget(output_group);

  auto *manual_group =
      new QGroupBox(QStringLiteral("MANUAL COMMISSIONING"), content);
  auto *manual_layout = new QVBoxLayout(manual_group);
  auto *di_layout = new QGridLayout();
  di_layout->addWidget(new QLabel(QStringLiteral("Channel"), manual_group), 0, 0);
  di_layout->addWidget(new QLabel(QStringLiteral("Observed"), manual_group), 0, 1);
  di_layout->addWidget(
      new QLabel(QStringLiteral("MOCK injection"), manual_group), 0, 2);
  for (std::size_t channel = 0; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    const int row = static_cast<int>(channel + 1);
    di_layout->addWidget(
        new QLabel(QStringLiteral("DI%1").arg(channel), manual_group), row, 0);
    modbus_di_values_[channel] =
        new QLabel(QStringLiteral("○ OFF"), manual_group);
    modbus_di_values_[channel]->setObjectName(
        QStringLiteral("modbusDi%1Value").arg(channel));
    modbus_di_injections_[channel] =
        new QCheckBox(QStringLiteral("Set ON"), manual_group);
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
  manual_layout->addLayout(di_layout);

  auto *do_layout = new QGridLayout();
  do_layout->addWidget(new QLabel(QStringLiteral("Manual"), manual_group), 0, 0);
  do_layout->addWidget(new QLabel(QStringLiteral("Requested"), manual_group), 0,
                       1);
  do_layout->addWidget(new QLabel(QStringLiteral("Confirmed"), manual_group), 0,
                       2);
  do_layout->addWidget(new QLabel(QStringLiteral("Reply"), manual_group), 0, 3);
  for (std::size_t channel = 1; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    const int row = static_cast<int>(channel);
    modbus_do_requests_[channel] = new QCheckBox(
        QStringLiteral("DO%1").arg(channel), manual_group);
    modbus_do_requests_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Request").arg(channel));
    modbus_do_requested_[channel] =
        new QLabel(QStringLiteral("OFF"), manual_group);
    modbus_do_requested_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Requested").arg(channel));
    modbus_do_confirmed_[channel] =
        new QLabel(QStringLiteral("OFF"), manual_group);
    modbus_do_confirmed_[channel]->setObjectName(
        QStringLiteral("modbusDo%1Confirmed").arg(channel));
    modbus_do_status_[channel] =
        new QLabel(QStringLiteral("NONE"), manual_group);
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
  manual_layout->addLayout(do_layout);
  modbus_all_off_ = new QPushButton(
      QStringLiteral("All Manual Outputs OFF"), manual_group);
  modbus_all_off_->setObjectName(QStringLiteral("modbusAllOffButton"));
  modbus_reply_ = new QLabel(QStringLiteral("No command"), manual_group);
  modbus_reply_->setObjectName(QStringLiteral("modbusReplyValue"));
  modbus_reply_->setWordWrap(true);
  manual_layout->addWidget(modbus_all_off_);
  manual_layout->addWidget(modbus_reply_);
  connect(modbus_all_off_, &QPushButton::clicked, &controller_,
          &WorkbenchController::requestAllOutputsOff);
  layout->addWidget(manual_group);

  auto *advanced = new QGroupBox(QStringLiteral("ADVANCED"), content);
  auto *advanced_layout = new QVBoxLayout(advanced);
  auto *banner = new QLabel(
      controller_.cellPeerMode()
          ? QStringLiteral(
                "Portfolio cell-peer: DO0 is edge-owned. Probe is optional commissioning.")
          : QStringLiteral(
                "MOCK / NO PHYSICAL RS485 — select PHYSICAL to probe the ARM agent"),
      advanced);
  banner->setObjectName(QStringLiteral("modbusMockBanner"));
  banner->setStyleSheet(QStringLiteral("font-weight: bold;"));
  advanced_layout->addWidget(banner);
  modbus_evidence_ = banner;

  auto *backend_row = new QHBoxLayout();
  modbus_select_mock_ = new QPushButton(QStringLiteral("MOCK"), advanced);
  modbus_select_mock_->setObjectName(QStringLiteral("modbusSelectMockButton"));
  modbus_select_physical_ =
      new QPushButton(QStringLiteral("PHYSICAL"), advanced);
  modbus_select_physical_->setObjectName(
      QStringLiteral("modbusSelectPhysicalButton"));
  backend_row->addWidget(modbus_select_mock_);
  backend_row->addWidget(modbus_select_physical_);
  backend_row->addStretch();
  advanced_layout->addLayout(backend_row);
  connect(modbus_select_mock_, &QPushButton::clicked, &controller_,
          &WorkbenchController::selectMockModbusBackend);
  connect(modbus_select_physical_, &QPushButton::clicked, &controller_,
          &WorkbenchController::selectPhysicalModbusBackend);

  auto *connection = new QFormLayout();
  modbus_backend_ = new QLabel(QStringLiteral("MOCK"), advanced);
  modbus_transport_ = new QLabel(QStringLiteral("Modbus RTU"), advanced);
  modbus_agent_peer_ =
      new QLineEdit(QStringLiteral("192.168.1.22:5740"), advanced);
  modbus_agent_peer_->setObjectName(QStringLiteral("modbusAgentPeerEdit"));
  modbus_serial_port_ = new QLabel(QStringLiteral("NOT CONNECTED"), advanced);
  modbus_baud_ = new QLabel(QStringLiteral("9600"), advanced);
  modbus_parity_ = new QLabel(QStringLiteral("None"), advanced);
  modbus_slave_ = new QLabel(QStringLiteral("1"), advanced);
  modbus_sku_ = new QLabel(QStringLiteral("n/a"), advanced);
  modbus_status_ = new QLabel(QStringLiteral("MOCK ONLINE"), advanced);
  modbus_status_->setObjectName(QStringLiteral("modbusDeviceStatus"));
  modbus_rtt_ = new QLabel(QStringLiteral("n/a"), advanced);
  connection->addRow(QStringLiteral("Backend"), modbus_backend_);
  connection->addRow(QStringLiteral("Transport"), modbus_transport_);
  connection->addRow(QStringLiteral("Agent peer"), modbus_agent_peer_);
  connection->addRow(QStringLiteral("Serial port"), modbus_serial_port_);
  connection->addRow(QStringLiteral("9600 8N1"), modbus_baud_);
  connection->addRow(QStringLiteral("Parity"), modbus_parity_);
  connection->addRow(QStringLiteral("Slave"), modbus_slave_);
  connection->addRow(QStringLiteral("SKU"), modbus_sku_);
  connection->addRow(QStringLiteral("Status"), modbus_status_);
  connection->addRow(QStringLiteral("RTT"), modbus_rtt_);
  advanced_layout->addLayout(connection);
  connect(modbus_agent_peer_, &QLineEdit::editingFinished, this, [this] {
    controller_.setModbusAgentPeer(modbus_agent_peer_->text());
  });

  auto *probe_row = new QHBoxLayout();
  modbus_scan_ =
      new QPushButton(QStringLiteral("Scan Slaves (MOCK)"), advanced);
  modbus_scan_->setObjectName(QStringLiteral("modbusScanButton"));
  modbus_disconnect_ = new QPushButton(QStringLiteral("Disconnect"), advanced);
  modbus_disconnect_->setObjectName(QStringLiteral("modbusDisconnectButton"));
  probe_row->addWidget(modbus_scan_);
  probe_row->addWidget(modbus_disconnect_);
  advanced_layout->addLayout(probe_row);
  modbus_scan_summary_ = new QLabel(QStringLiteral("UNKNOWN"), advanced);
  modbus_scan_summary_->setObjectName(QStringLiteral("modbusScanSummary"));
  modbus_scan_summary_->setWordWrap(true);
  advanced_layout->addWidget(modbus_scan_summary_);
  connect(modbus_scan_, &QPushButton::clicked, &controller_,
          &WorkbenchController::requestModbusScan);
  connect(modbus_disconnect_, &QPushButton::clicked, &controller_,
          &WorkbenchController::disconnectPhysicalModbus);
  layout->addWidget(advanced);
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
  device_->setText((snapshot.device.online ? QStringLiteral("ONLINE")
                                           : QStringLiteral("OFFLINE")));
  if (node_sensor_ != nullptr) {
    node_sensor_->setText(snapshot.position_reached
                              ? QStringLiteral("REACHED")
                              : QStringLiteral("NOT REACHED"));
  }
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
  overview_runtime_->setText(
      text(rcr::workbench::to_string(snapshot.runtime.mode)));
  if (overview_fault_ != nullptr) {
    overview_fault_->setText(
        text(rcr::workbench::to_string(snapshot.runtime.fault)));
  }
  overview_node_->setText(snapshot.device.online ? QStringLiteral("ONLINE")
                                                 : QStringLiteral("OFFLINE"));
  overview_position_reached_->setText(snapshot.position_reached
                                          ? QStringLiteral("REACHED")
                                          : QStringLiteral("NOT REACHED"));
  overview_cell_ready_->setText(snapshot.cell_ready ? QStringLiteral("TRUE")
                                                    : QStringLiteral("FALSE"));
  overview_do0_requested_->setText(
      snapshot.cell_ready_do0_requested ? QStringLiteral("ON")
                                        : QStringLiteral("OFF"));
  overview_do0_confirmed_->setText(
      snapshot.cell_ready_do0_confirmed ? QStringLiteral("ON")
                                        : QStringLiteral("OFF"));
  if (overview_heartbeat_ != nullptr) {
    overview_heartbeat_->setText(
        snapshot.device.heartbeat_age_ns < 0
            ? QStringLiteral("N/A")
            : QStringLiteral("%1 ms").arg(snapshot.device.heartbeat_age_ns /
                                          1'000'000));
  }
  if (overview_mr0_ != nullptr) {
    overview_mr0_->setText(snapshot.cell_modbus_online
                               ? QStringLiteral("ONLINE")
                               : QStringLiteral("OFFLINE"));
  }
  if (cell_ready_value_ != nullptr) {
    cell_ready_value_->setText(snapshot.cell_ready ? QStringLiteral("TRUE")
                                                   : QStringLiteral("FALSE"));
  }
  if (cell_connection_value_ != nullptr) {
    cell_connection_value_->setText(snapshot.cell_modbus_online
                                        ? QStringLiteral("ONLINE")
                                        : QStringLiteral("OFFLINE"));
  }
  if (controller_.cellPeerMode() && modbus_do_requested_[0] != nullptr) {
    modbus_do_requested_[0]->setText(
        snapshot.cell_ready_do0_requested ? QStringLiteral("ON")
                                          : QStringLiteral("OFF"));
    modbus_do_confirmed_[0]->setText(
        snapshot.cell_ready_do0_confirmed ? QStringLiteral("ON")
                                          : QStringLiteral("OFF"));
    modbus_do_status_[0]->setText(text(rcr::workbench::to_string(
        static_cast<rcr::workbench::ModbusIoCommandStatus>(
            snapshot.cell_ready_do0_status))));
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

void MainWindow::showRuntimeCommand(const rcr::workbench::CommandReply &reply) {
  if (last_command_ == nullptr) {
    return;
  }
  last_command_->setText(
      text(rcr::workbench::to_string(reply.status)) + QStringLiteral("  ") +
      text(rcr::workbench::to_string(reply.from_state)) + QStringLiteral(" → ") +
      text(rcr::workbench::to_string(reply.to_state)) + QStringLiteral("  ") +
      text(reply.message));
}

void MainWindow::updateModbus(
    const rcr::workbench::ModbusIoSnapshot &snapshot) {
  const bool physical =
      snapshot.evidence == rcr::workbench::EvidenceClass::Physical;
  modbus_backend_->setText(text(snapshot.backend));
  modbus_transport_->setText(text(snapshot.transport));
  if (modbus_agent_peer_ != nullptr &&
      modbus_agent_peer_->text() != text(snapshot.agent_peer) &&
      !snapshot.agent_peer.empty() && snapshot.agent_peer != "n/a") {
    const QSignalBlocker peer_blocker{modbus_agent_peer_};
    modbus_agent_peer_->setText(text(snapshot.agent_peer));
  }
  modbus_serial_port_->setText(text(snapshot.serial_port));
  if (physical) {
    modbus_baud_->setText(QString::number(snapshot.baud_rate));
    modbus_parity_->setText(text(snapshot.parity));
    modbus_status_->setText(text(rcr::workbench::to_string(snapshot.device_state)));
    modbus_scan_->setText(QStringLiteral("Probe"));
    modbus_sku_->setText(snapshot.sku.empty() ? QStringLiteral("MR0-IOR08")
                                              : text(snapshot.sku));
  } else {
    modbus_baud_->setText(
        QStringLiteral("%1 (placeholder)").arg(snapshot.baud_rate_placeholder));
    modbus_parity_->setText(text(snapshot.parity_placeholder));
    modbus_status_->setText(QStringLiteral("MOCK %1").arg(
        text(rcr::workbench::to_string(snapshot.device_state))));
    modbus_scan_->setText(QStringLiteral("Scan Slaves (MOCK)"));
    modbus_sku_->setText(QStringLiteral("n/a"));
  }
  if (controller_.cellPeerMode()) {
    modbus_evidence_->setText(QStringLiteral(
        "Portfolio cell-peer: DO0 is edge-owned via CEL1. Probe/DO1–DO3 are optional commissioning."));
  } else if (physical) {
    modbus_evidence_->setText(
        QStringLiteral("PHYSICAL MODBUS RTU — Qt on PC, RTU master on Orange Pi"));
  } else {
    modbus_evidence_->setText(
        QStringLiteral("MOCK / NO PHYSICAL RS485 — Modbus RTU hardware not in this backend"));
  }
  modbus_slave_->setText(QString::number(snapshot.slave_id));
  if (snapshot.last_transaction.rtt_ns > 0) {
    modbus_rtt_->setText(
        QStringLiteral("%1 ms").arg(snapshot.last_transaction.rtt_ns / 1'000'000));
  } else {
    modbus_rtt_->setText(QStringLiteral("n/a"));
  }

  QString scan = text(rcr::workbench::to_string(snapshot.scan_state));
  for (const auto &slave : snapshot.slaves) {
    scan += QStringLiteral("\nSlave %1  %2")
                .arg(slave.slave_id)
                .arg(text(rcr::workbench::to_string(slave.state)));
  }
  if (!snapshot.last_error.empty()) {
    scan += QStringLiteral("\n") + text(snapshot.last_error);
  }
  if (!snapshot.last_transaction.tx_hex.empty()) {
    scan += QStringLiteral("\nTX ") + text(snapshot.last_transaction.tx_hex);
  }
  if (!snapshot.last_transaction.rx_hex.empty()) {
    scan += QStringLiteral("\nRX ") + text(snapshot.last_transaction.rx_hex);
  }
  modbus_scan_summary_->setText(scan);
  modbus_scan_->setEnabled(snapshot.scan_state !=
                           rcr::workbench::ModbusScanState::Scanning);

  for (std::size_t channel = 0; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    const auto &input = snapshot.digital_inputs[channel];
    modbus_di_values_[channel]->setText(input.active ? QStringLiteral("● ON")
                                                     : QStringLiteral("○ OFF"));
    modbus_di_injections_[channel]->setEnabled(!physical);
    const QSignalBlocker input_blocker{modbus_di_injections_[channel]};
    modbus_di_injections_[channel]->setChecked(input.active);

    const auto &output = snapshot.digital_outputs[channel];
    const bool outputs_live =
        !physical ||
        snapshot.device_state == rcr::workbench::ModbusDeviceState::Online;
    if (channel == 0) {
      modbus_do_requests_[channel]->setEnabled(false);
    } else {
      modbus_do_requests_[channel]->setEnabled(outputs_live);
    }
    const QSignalBlocker output_blocker{modbus_do_requests_[channel]};
    modbus_do_requests_[channel]->setChecked(output.requested);
    if (!(controller_.cellPeerMode() && channel == 0)) {
      modbus_do_requested_[channel]->setText(
          output.requested ? QStringLiteral("ON") : QStringLiteral("OFF"));
      modbus_do_confirmed_[channel]->setText(
          output.confirmed ? QStringLiteral("ON") : QStringLiteral("OFF"));
      modbus_do_status_[channel]->setText(
          text(rcr::workbench::to_string(output.last_status)));
    }
  }
  if (modbus_all_off_ != nullptr) {
    const bool outputs_live =
        !physical ||
        snapshot.device_state == rcr::workbench::ModbusDeviceState::Online;
    modbus_all_off_->setEnabled(outputs_live);
  }
}

void MainWindow::showModbusReply(
    const rcr::workbench::ModbusIoCommandReply &reply) {
  modbus_reply_->setText(text(rcr::workbench::to_string(reply.status)) +
                         QStringLiteral(": ") + text(reply.message));
}

void MainWindow::updateRemoteConnection(
    const rcr::workbench::RemoteConnectionSnapshot &snapshot) {
  remote_banner_->setText(text(snapshot.evidence_banner));
  remote_mode_->setText(text(rcr::workbench::to_string(snapshot.mode)));
  remote_peer_->setText(text(snapshot.peer));
  remote_session_->setText(
      text(rcr::workbench::to_string(snapshot.session_state)));
  remote_heartbeat_->setText(
      QStringLiteral("ok %1 / missed %2")
          .arg(snapshot.heartbeats_ok)
          .arg(snapshot.heartbeats_missed));
  remote_status_count_->setText(QString::number(snapshot.status_ok));
  if (snapshot.connected) {
    remote_status_mode_->setText(
        text(rcr::workbench::to_string(snapshot.last_status.mode)));
  } else {
    remote_status_mode_->setText(QStringLiteral("n/a"));
  }
  remote_last_error_->setText(snapshot.last_error.empty()
                                  ? QStringLiteral("-")
                                  : text(snapshot.last_error));
  remote_connect_->setEnabled(
      snapshot.mode == rcr::workbench::RemoteBackendMode::RemoteLoopback &&
      !snapshot.connected);
  remote_disconnect_->setEnabled(snapshot.connected);
}
