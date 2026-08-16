// Controller 把“快读”留在 UI 线程，把“会等的事”丢到 worker。
// runHealth 调用的是和 headless CTest 同一套 CanCommunicationHealthTest /
// ResultWriter。

#include "controller/workbench_controller.hpp"

#include "rcr/workbench/application/cell_app_protocol.hpp"
#include "rcr/workbench/application/cell_ready_mapper.hpp"
#include "rcr/workbench/application/runtime_application_adapter.hpp"
#include "rcr/workbench/services/can_health_test.hpp"
#include "rcr/workbench/services/result_writer.hpp"
#include "rcr/result.hpp"

#include <QDateTime>

#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

namespace {

rcr::workbench::ModbusIoSnapshot make_unprobed_physical(const QString &peer) {
  rcr::workbench::ModbusIoSnapshot snapshot;
  snapshot.backend = "PHYSICAL";
  snapshot.evidence = rcr::workbench::EvidenceClass::Physical;
  snapshot.no_physical_rs485 = false;
  snapshot.transport = "Modbus RTU";
  snapshot.serial_port = "NOT CONNECTED";
  snapshot.agent_peer = peer.toStdString();
  snapshot.sku = "MR0-IOR08";
  snapshot.baud_rate = 9600;
  snapshot.baud_rate_placeholder = 9600;
  snapshot.parity = "None";
  snapshot.parity_placeholder = "None";
  snapshot.device_state = rcr::workbench::ModbusDeviceState::Unknown;
  snapshot.scan_state = rcr::workbench::ModbusScanState::Unknown;
  for (std::size_t channel = 0; channel < rcr::workbench::kModbusIoChannelCount;
       ++channel) {
    snapshot.digital_inputs[channel].channel =
        static_cast<std::uint8_t>(channel);
    snapshot.digital_outputs[channel].channel =
        static_cast<std::uint8_t>(channel);
  }
  return snapshot;
}

rcr::workbench::CommandStatus map_command_status(rcr::Errc code) noexcept {
  switch (code) {
  case rcr::Errc::Ok:
    return rcr::workbench::CommandStatus::Accepted;
  case rcr::Errc::InvalidArgument:
    return rcr::workbench::CommandStatus::InvalidArgument;
  case rcr::Errc::NotOpen:
    return rcr::workbench::CommandStatus::NotOpen;
  case rcr::Errc::IoError:
  case rcr::Errc::WouldBlock:
    return rcr::workbench::CommandStatus::IoError;
  case rcr::Errc::Timeout:
    return rcr::workbench::CommandStatus::Timeout;
  case rcr::Errc::Busy:
    return rcr::workbench::CommandStatus::Busy;
  case rcr::Errc::Rejected:
    return rcr::workbench::CommandStatus::Rejected;
  case rcr::Errc::Unsupported:
    return rcr::workbench::CommandStatus::Unsupported;
  }
  return rcr::workbench::CommandStatus::Rejected;
}

HealthTestWorker *make_health_worker(
    rcr::workbench::RuntimeApplicationAdapter *adapter,
    rcr::workbench::CellAppClient *cell_client,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory) {
  if (adapter != nullptr) {
    return new HealthTestWorker(*adapter, std::move(provenance),
                                std::move(result_directory));
  }
  if (cell_client != nullptr && cell_client->peer_port() != 0) {
    return new HealthTestWorker(cell_client->peer_host(), cell_client->peer_port(),
                                std::move(provenance),
                                std::move(result_directory));
  }
  return nullptr;
}

} // namespace

HealthTestWorker::HealthTestWorker(
    rcr::workbench::RuntimeApplicationAdapter &adapter,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory)
    : adapter_(&adapter), provenance_(std::move(provenance)),
      result_directory_(std::move(result_directory)) {}

HealthTestWorker::HealthTestWorker(
    std::string cell_host, std::uint16_t cell_port,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory)
    : provenance_(std::move(provenance)),
      result_directory_(std::move(result_directory)),
      cell_host_(std::move(cell_host)), cell_port_(cell_port) {}

void HealthTestWorker::requestCancel() { runner_.request_cancel(); }

void HealthTestWorker::runHealth(const QString &run_id) {
  // 同步跑完才 Q_EMIT。这段时间本线程的 event loop 不转，所以 Cancel 不能再
  // queued 进来。--cell-peer 用独立 CEL1 连接采样，不和 UI 抢同一条 TCP。
  rcr::workbench::CanHealthCriteria criteria{};
  rcr::workbench::CellAppClient peer;
  rcr::workbench::CanCommunicationHealthTest health =
      adapter_ != nullptr
          ? rcr::workbench::CanCommunicationHealthTest{*adapter_}
          : rcr::workbench::CanCommunicationHealthTest{
                [&peer] {
                  auto status =
                      peer.get_status(std::chrono::milliseconds{400});
                  if (!status) {
                    return rcr::workbench::RuntimeTelemetrySnapshot{};
                  }
                  return rcr::workbench::cell_status_to_snapshot(
                      status.value());
                },
                [](std::chrono::nanoseconds duration) {
                  std::this_thread::sleep_for(duration);
                  return rcr::Result<void>::success();
                }};

  if (adapter_ != nullptr) {
    criteria.expected_evidence = adapter_->evidence_class();
  } else {
    auto connected =
        peer.connect(cell_host_, cell_port_, std::chrono::milliseconds{1000});
    if (!connected) {
      rcr::workbench::TestResult failed;
      failed.run_id = run_id.toStdString();
      failed.case_id = "can.communication_health";
      failed.case_name = "CAN Communication Health";
      failed.outcome = rcr::workbench::TestOutcome::Error;
      failed.reason = connected.error().message();
      failed.provenance = provenance_;
      Q_EMIT completed(failed, {}, {}, {});
      return;
    }
    const auto first = peer.get_status(std::chrono::milliseconds{400});
    if (first && first.value().evidence !=
                     rcr::workbench::EvidenceClass::Unspecified) {
      criteria.expected_evidence = first.value().evidence;
    } else {
      criteria.expected_evidence = rcr::workbench::EvidenceClass::Physical;
    }
  }

  auto result = health.run(runner_, run_id.toStdString(), criteria);
  result.provenance = provenance_;

  QString json_path;
  QString csv_path;
  QString persistence_error;
  rcr::workbench::ResultWriter writer;
  const auto written = writer.write(result, result_directory_);
  if (written) {
    json_path = QString::fromStdString(written.value().json_path);
    csv_path = QString::fromStdString(written.value().csv_path);
  } else {
    persistence_error = QString::fromStdString(written.error().message());
  }

  Q_EMIT completed(result, json_path, csv_path, persistence_error);
}

void ModbusAgentWorker::disconnectClient() {
  client_.disconnect();
  last_host_.clear();
  last_port_ = 0;
}

void ModbusAgentWorker::emitTransportFailure(const rcr::Error &error,
                                             const QString &peer) {
  rcr::workbench::ModbusIoCommandReply reply;
  reply.status = error.code() == rcr::Errc::Timeout
                     ? rcr::workbench::ModbusIoCommandStatus::Timeout
                     : rcr::workbench::ModbusIoCommandStatus::Rejected;
  reply.message = error.message();
  auto snapshot = make_unprobed_physical(peer);
  snapshot.last_error = reply.message;
  snapshot.device_state = reply.status == rcr::workbench::ModbusIoCommandStatus::Timeout
                              ? rcr::workbench::ModbusDeviceState::Timeout
                              : rcr::workbench::ModbusDeviceState::Error;
  snapshot.last_command_status = reply.status;
  Q_EMIT transactionFinished(snapshot, reply, true);
}

void ModbusAgentWorker::emitResult(
    rcr::Result<rcr::workbench::ModbusIoSnapshot> snapshot, const QString &peer,
    bool user_visible) {
  if (!snapshot) {
    emitTransportFailure(snapshot.error(), peer);
    return;
  }
  snapshot.value().agent_peer = peer.toStdString();
  rcr::workbench::ModbusIoCommandReply reply;
  reply.status = snapshot.value().last_command_status;
  reply.message = snapshot.value().last_error.empty()
                      ? QStringLiteral("PHYSICAL transaction confirmed").toStdString()
                      : snapshot.value().last_error;
  const bool visible =
      user_visible ||
      snapshot.value().device_state != rcr::workbench::ModbusDeviceState::Online;
  Q_EMIT transactionFinished(snapshot.value(), reply, visible);
}

void ModbusAgentWorker::probe(const QString &host, quint16 port) {
  const QString peer = host + QChar(':') + QString::number(port);
  if (!client_.connected() || host != last_host_ || port != last_port_) {
    auto connected =
        client_.connect(host.toStdString(), port, std::chrono::milliseconds{500});
    if (!connected) {
      emitTransportFailure(connected.error(), peer);
      return;
    }
    last_host_ = host;
    last_port_ = port;
  }
  emitResult(client_.probe(std::chrono::milliseconds{1000}), peer, true);
}

void ModbusAgentWorker::readDi() {
  const QString peer = last_host_ + QChar(':') + QString::number(last_port_);
  if (!client_.connected()) {
    emitTransportFailure(rcr::Error{rcr::Errc::NotOpen, "agent client not connected"},
                         peer);
    return;
  }
  emitResult(client_.read_inputs(std::chrono::milliseconds{1000}), peer, false);
}

void ModbusAgentWorker::writeDo(int channel, bool active) {
  const QString peer = last_host_ + QChar(':') + QString::number(last_port_);
  if (!client_.connected()) {
    emitTransportFailure(rcr::Error{rcr::Errc::NotOpen, "agent client not connected"},
                         peer);
    return;
  }
  emitResult(client_.write_output(static_cast<std::uint8_t>(channel), active,
                                  std::chrono::milliseconds{1000}),
             peer, true);
}

void ModbusAgentWorker::allOff() {
  const QString peer = last_host_ + QChar(':') + QString::number(last_port_);
  if (!client_.connected()) {
    emitTransportFailure(rcr::Error{rcr::Errc::NotOpen, "agent client not connected"},
                         peer);
    return;
  }
  emitResult(client_.write_all_outputs_off(std::chrono::milliseconds{2000}), peer,
             true);
}

WorkbenchController::WorkbenchController(
    rcr::workbench::RuntimeApplicationAdapter &adapter,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory,
    QObject *parent)
    : WorkbenchController(&adapter, nullptr, std::move(provenance),
                          std::move(result_directory), parent) {}

WorkbenchController::WorkbenchController(
    rcr::workbench::CellAppClient &cell_client,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory,
    QObject *parent)
    : WorkbenchController(nullptr, &cell_client, std::move(provenance),
                          std::move(result_directory), parent) {}

WorkbenchController::WorkbenchController(
    rcr::workbench::RuntimeApplicationAdapter *adapter,
    rcr::workbench::CellAppClient *cell_client,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory,
    QObject *parent)
    : QObject(parent), adapter_(adapter), cell_client_(cell_client),
      worker_(make_health_worker(adapter, cell_client, provenance,
                                 result_directory)),
      modbus_worker_(new ModbusAgentWorker()) {
  // DECLARE 在头文件，REGISTER 在进程里做一次：跨线程排队时 Qt 才能拷贝这些
  // DTO。
  qRegisterMetaType<rcr::workbench::RuntimeTelemetrySnapshot>();
  qRegisterMetaType<rcr::workbench::TestResult>();
  qRegisterMetaType<rcr::workbench::ActuatorSnapshot>();
  qRegisterMetaType<rcr::workbench::ActuatorCommandReply>();
  qRegisterMetaType<rcr::workbench::ModbusIoSnapshot>();
  qRegisterMetaType<rcr::workbench::ModbusIoCommandReply>();
  qRegisterMetaType<rcr::workbench::RemoteConnectionSnapshot>();
  qRegisterMetaType<rcr::workbench::CommandReply>();

  remote_client_.set_endpoint(&remote_endpoint_);
  remote_elapsed_.start();
  resetPhysicalSnapshot();

  // worker_ 在本线程 new，再搬到 worker_thread_。之后 runHealth 只在那边跑。
  // --cell-peer 用第二条 CEL1 连接采样边缘快照，不打开 ThinkPad SocketCAN。
  if (worker_ != nullptr) {
    worker_->moveToThread(&worker_thread_);
    connect(this, &WorkbenchController::healthRequested, worker_,
            &HealthTestWorker::runHealth, Qt::QueuedConnection);
    connect(
        worker_, &HealthTestWorker::completed, this,
        [this](const rcr::workbench::TestResult &result, const QString &json_path,
               const QString &csv_path, const QString &persistence_error) {
          health_running_ = false;
          Q_EMIT healthCompleted(result, json_path, csv_path, persistence_error);
        },
        Qt::QueuedConnection);
    connect(&worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);
    worker_thread_.start();
  }

  modbus_worker_->moveToThread(&modbus_thread_);
  connect(this, &WorkbenchController::physicalModbusProbeRequested,
          modbus_worker_, &ModbusAgentWorker::probe, Qt::QueuedConnection);
  connect(this, &WorkbenchController::physicalModbusReadDiRequested,
          modbus_worker_, &ModbusAgentWorker::readDi, Qt::QueuedConnection);
  connect(this, &WorkbenchController::physicalModbusWriteDoRequested,
          modbus_worker_, &ModbusAgentWorker::writeDo, Qt::QueuedConnection);
  connect(this, &WorkbenchController::physicalModbusAllOffRequested,
          modbus_worker_, &ModbusAgentWorker::allOff, Qt::QueuedConnection);
  connect(modbus_worker_, &ModbusAgentWorker::transactionFinished, this,
          &WorkbenchController::applyPhysicalTransaction, Qt::QueuedConnection);
  connect(&modbus_thread_, &QThread::finished, modbus_worker_,
          &QObject::deleteLater);
  modbus_thread_.start();

  snapshot_timer_.setInterval(std::chrono::milliseconds{100});
  connect(&snapshot_timer_, &QTimer::timeout, this,
          &WorkbenchController::publishSnapshot);
  snapshot_timer_.start();

  // 10 ms 只驱动确定性 Mock，不承担真实控制。QElapsedTimer 提供单调 elapsed，
  // profile 内部会限制物理积分步长，Jog lease 仍按完整 elapsed 失效。
  actuator_timer_.setTimerType(Qt::PreciseTimer);
  actuator_timer_.setInterval(std::chrono::milliseconds{10});
  connect(&actuator_timer_, &QTimer::timeout, this,
          &WorkbenchController::tickActuator);
  actuator_elapsed_.start();
  actuator_timer_.start();
  modbus_elapsed_.start();

  jog_renew_timer_.setInterval(std::chrono::milliseconds{50});
  connect(&jog_renew_timer_, &QTimer::timeout, this,
          &WorkbenchController::renewJog);

  // 约 500 ms 的 commissioning 轮询，不是控制环；忙则跳过，不排队。
  modbus_poll_timer_.setTimerType(Qt::CoarseTimer);
  modbus_poll_timer_.setInterval(std::chrono::milliseconds{500});
  connect(&modbus_poll_timer_, &QTimer::timeout, this,
          &WorkbenchController::tickPhysicalModbusPoll);
  modbus_poll_timer_.start();
}

WorkbenchController::~WorkbenchController() {
  // 先停刷新，再取消测试，再 quit/wait。先析构 Window 再走到这里；daemon.stop()
  // 更晚。
  snapshot_timer_.stop();
  actuator_timer_.stop();
  jog_renew_timer_.stop();
  modbus_poll_timer_.stop();
  remote_client_.disconnect_session();
  if (modbus_worker_ != nullptr) {
    modbus_worker_->disconnectClient();
  }
  if (worker_ != nullptr) {
    worker_->requestCancel();
  }
  worker_thread_.quit();
  worker_thread_.wait();
  worker_ = nullptr;
  modbus_thread_.quit();
  modbus_thread_.wait();
  modbus_worker_ = nullptr;
}

void WorkbenchController::startHealth() {
  if (worker_ == nullptr || health_running_) {
    return;
  }
  health_running_ = true;
  Q_EMIT healthStarted();
  // 墙钟只拼 run id / 文件名；观察窗和 heartbeat 判定仍用 CLOCK_MONOTONIC。
  const auto suffix = QDateTime::currentMSecsSinceEpoch();
  Q_EMIT healthRequested(QStringLiteral("qt-can-health-%1").arg(suffix));
}

void WorkbenchController::publishCurrentState() { publishSnapshot(); }

void WorkbenchController::cancelHealth() {
  if (health_running_ && worker_ != nullptr) {
    worker_->requestCancel();
  }
}

void WorkbenchController::activateRuntime() {
  if (cell_client_ != nullptr) {
    if (!ensureCellPeerConnected()) {
      publishRuntimeCommand(
          {rcr::workbench::CommandStatus::NotOpen,
           last_runtime_snapshot_.runtime.mode,
           last_runtime_snapshot_.runtime.mode, "CEL1 not connected"});
      return;
    }
    publishCellCommand(
        cell_client_->activate(std::chrono::milliseconds{1500}));
  } else if (adapter_ != nullptr) {
    publishRuntimeCommand(adapter_->activate());
  }
  publishSnapshot();
}

void WorkbenchController::commandServoHome() { submitServoBit(false); }

void WorkbenchController::commandServoTarget() { submitServoBit(true); }

void WorkbenchController::submitServoBit(bool target_position) {
  const auto snap =
      cell_client_ != nullptr
          ? last_runtime_snapshot_
          : (adapter_ != nullptr ? adapter_->snapshot()
                                 : rcr::workbench::RuntimeTelemetrySnapshot{});
  rcr::workbench::DigitalOutputRequest request{};
  request.session_id = snap.device.session_id;
  request.sequence = static_cast<std::uint64_t>(++servo_command_sequence_);
  if (request.sequence == 0) {
    request.sequence = ++servo_command_sequence_;
  }
  request.valid_for_ms = 2000;
  request.mask = 0x01;
  request.values = target_position ? 0x01U : 0x00U;
  if (cell_client_ != nullptr) {
    if (!ensureCellPeerConnected()) {
      publishRuntimeCommand(
          {rcr::workbench::CommandStatus::NotOpen,
           last_runtime_snapshot_.runtime.mode,
           last_runtime_snapshot_.runtime.mode, "CEL1 not connected"});
      return;
    }
    publishCellCommand(cell_client_->submit_output(
        request, std::chrono::milliseconds{1500}));
  } else if (adapter_ != nullptr) {
    publishRuntimeCommand(adapter_->submit_digital_output(request));
  }
  publishSnapshot();
}

void WorkbenchController::publishRuntimeCommand(
    const rcr::workbench::CommandReply &reply) {
  Q_EMIT runtimeCommandCompleted(reply);
}

void WorkbenchController::publishCellCommand(
    const rcr::Result<rcr::workbench::CommandReply> &result) {
  if (result) {
    publishRuntimeCommand(result.value());
    return;
  }
  const auto mode = last_runtime_snapshot_.runtime.mode;
  publishRuntimeCommand(rcr::workbench::CommandReply{
      map_command_status(result.error().code()), mode, mode,
      result.error().message()});
}

bool WorkbenchController::ensureCellPeerConnected() {
  if (cell_client_ == nullptr) {
    return false;
  }
  if (cell_client_->connected()) {
    return true;
  }
  return cell_client_->reconnect(std::chrono::milliseconds{400}).ok();
}

void WorkbenchController::applyCellReadyOutput(
    const rcr::workbench::CellReadyDecision &decision) {
  const bool live =
      modbus_backend_ == ModbusBackend::Mock || physicalOutputsLive();
  if (!live) {
    cell_ready_mapper_.note_modbus_offline();
    return;
  }
  if (modbus_request_running_) {
    return;
  }
  const auto action = cell_ready_mapper_.observe(decision, true);
  if (action == rcr::workbench::CellReadyDo0Action::RequestOn) {
    requestDigitalOutput(0, true);
  } else if (action == rcr::workbench::CellReadyDo0Action::RequestOff) {
    requestDigitalOutput(0, false);
  }
}

void WorkbenchController::driveEnable() {
  publishActuatorReply(actuator_.drive_enable());
}

void WorkbenchController::driveDisable() {
  active_jog_token_ = 0;
  jog_renew_timer_.stop();
  publishActuatorReply(actuator_.drive_disable());
}

void WorkbenchController::homeActuator() {
  publishActuatorReply(actuator_.home());
}

void WorkbenchController::startActuatorVelocity(double velocity_rad_s) {
  publishActuatorReply(actuator_.start_velocity(velocity_rad_s));
}

void WorkbenchController::normalStopActuator() {
  active_jog_token_ = 0;
  jog_renew_timer_.stop();
  publishActuatorReply(actuator_.normal_stop());
}

void WorkbenchController::quickStopActuator() {
  active_jog_token_ = 0;
  jog_renew_timer_.stop();
  publishActuatorReply(actuator_.quick_stop());
}

void WorkbenchController::jogPressed(int direction, double velocity_rad_s) {
  // token + 50 ms renew：UI 卡住或窗口关掉后 lease 会过期，Mock 不会一直转。
  if (active_jog_token_ != 0) {
    return;
  }
  const auto reply = actuator_.jog_press(direction, velocity_rad_s);
  if (reply.accepted()) {
    active_jog_token_ = reply.token;
    jog_renew_timer_.start();
  }
  publishActuatorReply(reply);
}

void WorkbenchController::jogReleased() {
  jog_renew_timer_.stop();
  if (active_jog_token_ == 0) {
    return;
  }
  const auto token = active_jog_token_;
  active_jog_token_ = 0;
  publishActuatorReply(actuator_.jog_release(token));
}

void WorkbenchController::resetActuatorFault() {
  publishActuatorReply(actuator_.reset_fault());
}

void WorkbenchController::requestModbusScan() {
  if (modbus_backend_ == ModbusBackend::Physical) {
    if (modbus_request_running_) {
      return;
    }
    QString host;
    quint16 port = 0;
    if (!parseAgentPeer(&host, &port)) {
      physical_snapshot_.last_error = "invalid agent peer host:port";
      physical_snapshot_.device_state = rcr::workbench::ModbusDeviceState::Error;
      Q_EMIT modbusSnapshotReady(physical_snapshot_);
      return;
    }
    modbus_request_running_ = true;
    physical_snapshot_.scan_state = rcr::workbench::ModbusScanState::Scanning;
    physical_snapshot_.agent_peer = modbus_agent_peer_.toStdString();
    Q_EMIT modbusSnapshotReady(physical_snapshot_);
    Q_EMIT physicalModbusProbeRequested(host, port);
    return;
  }
  if (!modbus_io_.begin_scan(modbusNowNs())) {
    Q_EMIT modbusSnapshotReady(modbus_io_.snapshot());
    return;
  }
  Q_EMIT modbusSnapshotReady(modbus_io_.snapshot());

  // Mock completion 排到下一轮 event loop：MainWindow 从不执行 scan
  // loop；未来真实实现可把 这个 completion 换成 worker 信号，而不改变 UI
  // 请求/快照合同。
  QTimer::singleShot(0, this, [this] {
    static_cast<void>(modbus_io_.complete_scan(modbusNowNs()));
    if (modbus_backend_ == ModbusBackend::Mock) {
      Q_EMIT modbusSnapshotReady(modbus_io_.snapshot());
    }
  });
}

void WorkbenchController::setMockDigitalInput(int channel, bool active) {
  if (modbus_backend_ == ModbusBackend::Physical) {
    publishModbusReply({rcr::workbench::ModbusIoCommandStatus::Rejected,
                        rcr::workbench::kAllModbusIoChannels, active, false,
                        "PHYSICAL backend rejects Mock DI injection"});
    return;
  }
  if (channel < 0 ||
      channel >= static_cast<int>(rcr::workbench::kModbusIoChannelCount)) {
    publishModbusReply(
        {rcr::workbench::ModbusIoCommandStatus::InvalidChannel,
         rcr::workbench::kAllModbusIoChannels, active, false,
         "MOCK / NO PHYSICAL RS485: DI channel must be in range 0..3"});
    return;
  }
  publishModbusReply(modbus_io_.set_mock_digital_input(
      static_cast<std::size_t>(channel), active, modbusNowNs()));
}

void WorkbenchController::requestDigitalOutput(int channel, bool active) {
  // --cell-peer 下 DO0 只属于边缘 CellReadyMapper，Qt 不得当第二套自动 owner。
  if (cell_client_ != nullptr && channel == 0) {
    publishModbusReply({rcr::workbench::ModbusIoCommandStatus::Rejected,
                        0, active, false,
                        "DO0 is edge-owned Cell Ready output"});
    return;
  }
  if (modbus_backend_ == ModbusBackend::Physical) {
    if (physical_command_blocked_) {
      publishModbusReply(
          {rcr::workbench::ModbusIoCommandStatus::Rejected,
           rcr::workbench::kAllModbusIoChannels, active, false,
           "probe required after timeout; no stale DO replay"});
      return;
    }
    if (!physicalOutputsLive()) {
      publishModbusReply({rcr::workbench::ModbusIoCommandStatus::Rejected,
                          rcr::workbench::kAllModbusIoChannels, active, false,
                          "PHYSICAL DO requires ONLINE"});
      return;
    }
    if (modbus_request_running_) {
      publishModbusReply({rcr::workbench::ModbusIoCommandStatus::Busy,
                          static_cast<std::uint8_t>(channel), active, false,
                          "PHYSICAL request already in flight"});
      return;
    }
    if (channel < 0 ||
        channel >= static_cast<int>(rcr::workbench::kModbusIoChannelCount)) {
      publishModbusReply(
          {rcr::workbench::ModbusIoCommandStatus::InvalidChannel,
           rcr::workbench::kAllModbusIoChannels, active, false,
           "PHYSICAL DO channel must be in range 0..3"});
      return;
    }
    auto &output = physical_snapshot_.digital_outputs[static_cast<std::size_t>(channel)];
    output.requested = active;
    output.last_status = rcr::workbench::ModbusIoCommandStatus::None;
    Q_EMIT modbusSnapshotReady(physical_snapshot_);
    modbus_request_running_ = true;
    Q_EMIT physicalModbusWriteDoRequested(channel, active);
    return;
  }
  if (channel < 0 ||
      channel >= static_cast<int>(rcr::workbench::kModbusIoChannelCount)) {
    publishModbusReply(
        {rcr::workbench::ModbusIoCommandStatus::InvalidChannel,
         rcr::workbench::kAllModbusIoChannels, active, false,
         "MOCK / NO PHYSICAL RS485: DO channel must be in range 0..3"});
    return;
  }
  publishModbusReply(modbus_io_.write_digital_output(
      static_cast<std::size_t>(channel), active, modbusNowNs()));
}

void WorkbenchController::requestAllOutputsOff() {
  if (modbus_backend_ == ModbusBackend::Physical) {
    if (physical_command_blocked_) {
      publishModbusReply(
          {rcr::workbench::ModbusIoCommandStatus::Rejected,
           rcr::workbench::kAllModbusIoChannels, false, false,
           "probe required after timeout; no stale DO replay"});
      return;
    }
    if (!physicalOutputsLive()) {
      publishModbusReply({rcr::workbench::ModbusIoCommandStatus::Rejected,
                          rcr::workbench::kAllModbusIoChannels, false, false,
                          "PHYSICAL ALL OFF requires ONLINE"});
      return;
    }
    if (modbus_request_running_) {
      publishModbusReply({rcr::workbench::ModbusIoCommandStatus::Busy,
                          rcr::workbench::kAllModbusIoChannels, false, false,
                          "PHYSICAL request already in flight"});
      return;
    }
    for (auto &output : physical_snapshot_.digital_outputs) {
      output.requested = false;
      output.last_status = rcr::workbench::ModbusIoCommandStatus::None;
    }
    Q_EMIT modbusSnapshotReady(physical_snapshot_);
    modbus_request_running_ = true;
    Q_EMIT physicalModbusAllOffRequested();
    return;
  }
  publishModbusReply(modbus_io_.write_all_outputs_off(modbusNowNs()));
}

void WorkbenchController::setNextMockModbusWriteOutcome(
    rcr::workbench::ModbusIoCommandStatus outcome) {
  modbus_io_.set_next_write_outcome(outcome);
}

void WorkbenchController::setRemoteHeartbeatRepliesEnabled(bool enabled) {
  remote_endpoint_.set_heartbeat_replies_enabled(enabled);
}

void WorkbenchController::selectLocalBackend() {
  remote_mode_ = rcr::workbench::RemoteBackendMode::Local;
  remote_client_.disconnect_session();
  publishRemoteConnection();
}

void WorkbenchController::selectRemoteLoopbackBackend() {
  remote_mode_ = rcr::workbench::RemoteBackendMode::RemoteLoopback;
  // 只切换模式，不自动 HELLO；Connect 必须由操作者显式触发。
  if (remote_client_.connected()) {
    remote_client_.disconnect_session();
  }
  publishRemoteConnection();
}

void WorkbenchController::connectRemoteLoopback() {
  if (adapter_ == nullptr) {
    return;
  }
  if (remote_mode_ != rcr::workbench::RemoteBackendMode::RemoteLoopback) {
    remote_mode_ = rcr::workbench::RemoteBackendMode::RemoteLoopback;
  }
  const auto status =
      rcr::workbench::project_remote_status(adapter_->snapshot());
  static_cast<void>(remote_client_.connect_session(status));
  publishRemoteConnection();
}

void WorkbenchController::disconnectRemoteLoopback() {
  remote_client_.disconnect_session();
  publishRemoteConnection();
}

void WorkbenchController::selectMockModbusBackend() {
  if (modbus_backend_ == ModbusBackend::Mock) {
    publishActiveModbusSnapshot();
    return;
  }
  if (modbus_worker_ != nullptr) {
    modbus_worker_->disconnectClient();
  }
  modbus_backend_ = ModbusBackend::Mock;
  modbus_request_running_ = false;
  physical_command_blocked_ = false;
  cell_ready_mapper_.note_modbus_offline();
  publishActiveModbusSnapshot();
}

void WorkbenchController::selectPhysicalModbusBackend() {
  modbus_backend_ = ModbusBackend::Physical;
  physical_command_blocked_ = false;
  cell_ready_mapper_.note_modbus_offline();
  resetPhysicalSnapshot();
  publishActiveModbusSnapshot();
}

void WorkbenchController::setModbusAgentPeer(const QString &peer) {
  modbus_agent_peer_ = peer.trimmed();
  if (modbus_backend_ == ModbusBackend::Physical) {
    physical_snapshot_.agent_peer = modbus_agent_peer_.toStdString();
    publishActiveModbusSnapshot();
  }
}

void WorkbenchController::disconnectPhysicalModbus() {
  if (modbus_worker_ != nullptr) {
    modbus_worker_->disconnectClient();
  }
  modbus_request_running_ = false;
  physical_command_blocked_ = false;
  cell_ready_mapper_.note_modbus_offline();
  if (modbus_backend_ == ModbusBackend::Physical) {
    resetPhysicalSnapshot();
    physical_snapshot_.last_error = "disconnected";
    publishActiveModbusSnapshot();
  }
}

void WorkbenchController::publishSnapshot() {
  rcr::workbench::RuntimeTelemetrySnapshot snap;
  if (cell_client_ != nullptr) {
    if (!ensureCellPeerConnected()) {
      snap = last_runtime_snapshot_;
      Q_EMIT snapshotReady(snap);
      Q_EMIT actuatorSnapshotReady(actuator_.snapshot());
      publishActiveModbusSnapshot();
      tickRemoteLoopback();
      publishRemoteConnection();
      return;
    }
    // 工程站只消费边缘已经算完的 CellReady；本进程不再跑 mapper。
    auto status = cell_client_->get_status(std::chrono::milliseconds{80});
    if (status) {
      snap = rcr::workbench::cell_status_to_snapshot(status.value());
      last_runtime_snapshot_ = snap;
    } else {
      snap = last_runtime_snapshot_;
    }
  } else if (adapter_ != nullptr) {
    snap = adapter_->snapshot();
    const auto cell = rcr::workbench::evaluate_cell_ready(snap);
    snap.position_reached = cell.position_reached;
    snap.cell_ready = cell.cell_ready;
    applyCellReadyOutput(cell);
    const auto io = modbus_backend_ == ModbusBackend::Physical
                        ? physical_snapshot_
                        : modbus_io_.snapshot();
    snap.cell_modbus_online =
        io.device_state == rcr::workbench::ModbusDeviceState::Online;
    snap.cell_ready_do0_requested = io.digital_outputs[0].requested;
    snap.cell_ready_do0_confirmed = io.digital_outputs[0].confirmed;
    snap.cell_ready_do0_status =
        static_cast<std::uint8_t>(io.digital_outputs[0].last_status);
    last_runtime_snapshot_ = snap;
  }
  Q_EMIT snapshotReady(snap);
  Q_EMIT actuatorSnapshotReady(actuator_.snapshot());
  publishActiveModbusSnapshot();
  tickRemoteLoopback();
  publishRemoteConnection();
}

void WorkbenchController::publishActiveModbusSnapshot() {
  if (modbus_backend_ == ModbusBackend::Physical) {
    Q_EMIT modbusSnapshotReady(physical_snapshot_);
    return;
  }
  Q_EMIT modbusSnapshotReady(modbus_io_.snapshot());
}

void WorkbenchController::resetPhysicalSnapshot() {
  physical_snapshot_ = make_unprobed_physical(modbus_agent_peer_);
}

bool WorkbenchController::physicalOutputsLive() const {
  return modbus_backend_ == ModbusBackend::Physical &&
         !physical_command_blocked_ &&
         physical_snapshot_.device_state ==
             rcr::workbench::ModbusDeviceState::Online;
}

void WorkbenchController::tickPhysicalModbusPoll() {
  if (cell_client_ != nullptr) {
    return;
  }
  if (modbus_backend_ != ModbusBackend::Physical || modbus_request_running_ ||
      physical_command_blocked_ ||
      physical_snapshot_.device_state !=
          rcr::workbench::ModbusDeviceState::Online) {
    return;
  }
  modbus_request_running_ = true;
  Q_EMIT physicalModbusReadDiRequested();
}

void WorkbenchController::applyPhysicalTransaction(
    const rcr::workbench::ModbusIoSnapshot &snapshot,
    const rcr::workbench::ModbusIoCommandReply &reply, bool user_visible) {
  modbus_request_running_ = false;
  if (modbus_backend_ != ModbusBackend::Physical) {
    return;
  }
  physical_snapshot_ = snapshot;
  physical_snapshot_.agent_peer = modbus_agent_peer_.toStdString();
  physical_command_blocked_ =
      physical_snapshot_.device_state != rcr::workbench::ModbusDeviceState::Online;
  if (physical_command_blocked_) {
    cell_ready_mapper_.note_modbus_offline();
  }
  if (user_visible) {
    Q_EMIT modbusCommandCompleted(reply);
  }
  Q_EMIT modbusSnapshotReady(physical_snapshot_);
}

bool WorkbenchController::parseAgentPeer(QString *host, quint16 *port) const {
  const auto colon = modbus_agent_peer_.lastIndexOf(QChar(':'));
  if (colon <= 0 || host == nullptr || port == nullptr) {
    return false;
  }
  bool ok = false;
  const int parsed = modbus_agent_peer_.mid(colon + 1).toInt(&ok);
  if (!ok || parsed <= 0 || parsed > 65535) {
    return false;
  }
  *host = modbus_agent_peer_.left(colon);
  *port = static_cast<quint16>(parsed);
  return !host->isEmpty();
}

void WorkbenchController::publishRemoteConnection() {
  Q_EMIT remoteConnectionReady(remote_client_.snapshot(remote_mode_));
}

void WorkbenchController::tickRemoteLoopback() {
  if (adapter_ == nullptr ||
      remote_mode_ != rcr::workbench::RemoteBackendMode::RemoteLoopback ||
      !remote_client_.connected()) {
    return;
  }
  // 把最新 Runtime 投影灌进 endpoint，再经控制面协议读回——证明 UI 消费的是
  // RemoteStatusView，而不是直接摸 daemon 私有结构。
  remote_endpoint_.set_status(
      rcr::workbench::project_remote_status(adapter_->snapshot()));
  const auto now_ns =
      remote_elapsed_.isValid() ? remote_elapsed_.nsecsElapsed() : 0;
  static_cast<void>(remote_client_.poll_heartbeat(now_ns));
  static_cast<void>(remote_client_.refresh_status());
}

void WorkbenchController::tickActuator() {
  // 用实际流逝时间，不假设 QTimer 真的每 10 ms 响一次。Qt 定时器会抖、会合并。
  const auto elapsed_ns = actuator_elapsed_.nsecsElapsed();
  actuator_elapsed_.restart();
  actuator_.tick(std::chrono::nanoseconds{elapsed_ns});
  if (active_jog_token_ != 0 && actuator_.snapshot().active_jog_token == 0) {
    active_jog_token_ = 0;
    jog_renew_timer_.stop();
  }
}

void WorkbenchController::renewJog() {
  if (active_jog_token_ == 0) {
    jog_renew_timer_.stop();
    return;
  }
  const auto reply = actuator_.jog_renew(active_jog_token_);
  if (!reply.accepted()) {
    active_jog_token_ = 0;
    jog_renew_timer_.stop();
    publishActuatorReply(reply);
  }
}

void WorkbenchController::publishActuatorReply(
    const rcr::workbench::ActuatorCommandReply &reply) {
  Q_EMIT actuatorCommandCompleted(reply);
  Q_EMIT actuatorSnapshotReady(actuator_.snapshot());
}

void WorkbenchController::publishModbusReply(
    const rcr::workbench::ModbusIoCommandReply &reply) {
  Q_EMIT modbusCommandCompleted(reply);
  publishActiveModbusSnapshot();
}

std::int64_t WorkbenchController::modbusNowNs() const {
  // QElapsedTimer 使用单调时钟；只给 Mock 标注相对更新时间，不用于冒充 RTU
  // timeout。
  return modbus_elapsed_.isValid() ? modbus_elapsed_.nsecsElapsed() : 0;
}
