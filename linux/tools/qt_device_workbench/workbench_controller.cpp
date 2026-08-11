#include "workbench_controller.hpp"

#include "rcr/workbench/can_health_test.hpp"
#include "rcr/workbench/result_writer.hpp"
#include "rcr/workbench/runtime_application_adapter.hpp"

#include <QDateTime>

#include <chrono>
#include <cstdint>
#include <utility>

HealthTestWorker::HealthTestWorker(
    rcr::workbench::RuntimeApplicationAdapter &adapter,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory)
    : adapter_(adapter), provenance_(std::move(provenance)),
      result_directory_(std::move(result_directory)) {}

void HealthTestWorker::requestCancel() { runner_.request_cancel(); }

void HealthTestWorker::runHealth(const QString &run_id) {
  rcr::workbench::CanCommunicationHealthTest health{adapter_};
  rcr::workbench::CanHealthCriteria criteria{};
  criteria.expected_evidence = rcr::workbench::EvidenceClass::Vcan;

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

WorkbenchController::WorkbenchController(
    rcr::workbench::RuntimeApplicationAdapter &adapter,
    rcr::workbench::TestRunProvenance provenance, std::string result_directory,
    QObject *parent)
    : QObject(parent), adapter_(adapter),
      worker_(new HealthTestWorker(adapter, std::move(provenance),
                                   std::move(result_directory))) {
  qRegisterMetaType<rcr::workbench::RuntimeTelemetrySnapshot>();
  qRegisterMetaType<rcr::workbench::TestResult>();
  qRegisterMetaType<rcr::workbench::ActuatorSnapshot>();
  qRegisterMetaType<rcr::workbench::ActuatorCommandReply>();

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

  jog_renew_timer_.setInterval(std::chrono::milliseconds{50});
  connect(&jog_renew_timer_, &QTimer::timeout, this,
          &WorkbenchController::renewJog);
  publishSnapshot();
}

WorkbenchController::~WorkbenchController() {
  snapshot_timer_.stop();
  actuator_timer_.stop();
  jog_renew_timer_.stop();
  if (worker_ != nullptr) {
    worker_->requestCancel();
  }
  worker_thread_.quit();
  worker_thread_.wait();
  worker_ = nullptr;
}

void WorkbenchController::startHealth() {
  if (health_running_) {
    return;
  }
  health_running_ = true;
  Q_EMIT healthStarted();
  const auto suffix = QDateTime::currentMSecsSinceEpoch();
  Q_EMIT healthRequested(QStringLiteral("qt-vcan-health-%1").arg(suffix));
}

void WorkbenchController::cancelHealth() {
  if (health_running_ && worker_ != nullptr) {
    worker_->requestCancel();
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

void WorkbenchController::publishSnapshot() {
  Q_EMIT snapshotReady(adapter_.snapshot());
  Q_EMIT actuatorSnapshotReady(actuator_.snapshot());
}

void WorkbenchController::tickActuator() {
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
