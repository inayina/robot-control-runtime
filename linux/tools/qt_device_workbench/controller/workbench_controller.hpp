#pragma once

// Workbench 用例层：拉快照、跑测试、推进隔离 Mock / Physical commissioning。
// 不画控件，也不 start/stop daemon。
//
// 线程：
//   UI 线程 — 本对象、QTimer、Mock tick；只做快读和发请求。
//   CAN Health worker — 同步 Health + fsync。
//   Modbus agent worker — 阻塞 TCP Probe / 读 DI / 写 DO；不碰 QWidget，也不打开 tty。
//
// CMake 开了 QT_NO_KEYWORDS，所以写 Q_SLOTS / Q_SIGNALS / Q_EMIT。

#include "controller/qt_metatypes.hpp"

#include "rcr/workbench/application/cell_ready_mapper.hpp"
#include "rcr/workbench/services/cell_app_client.hpp"
#include "rcr/workbench/services/modbus_agent_client.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <string>

namespace rcr::workbench {
class RuntimeApplicationAdapter;
}

class HealthTestWorker final : public QObject {
  Q_OBJECT

public:
  HealthTestWorker(rcr::workbench::RuntimeApplicationAdapter &adapter,
                   rcr::workbench::TestRunProvenance provenance,
                   std::string result_directory);

  void requestCancel();

public Q_SLOTS:
  void runHealth(const QString &run_id);

Q_SIGNALS:
  void completed(const rcr::workbench::TestResult &result,
                 const QString &json_path, const QString &csv_path,
                 const QString &persistence_error);

private:
  rcr::workbench::RuntimeApplicationAdapter &adapter_;
  rcr::workbench::TestRunProvenance provenance_;
  std::string result_directory_;
  rcr::workbench::TestRunner runner_{};
};

class ModbusAgentWorker final : public QObject {
  Q_OBJECT

public:
  void disconnectClient();

public Q_SLOTS:
  void probe(const QString &host, quint16 port);
  void readDi();
  void writeDo(int channel, bool active);
  void allOff();

Q_SIGNALS:
  void transactionFinished(const rcr::workbench::ModbusIoSnapshot &snapshot,
                           const rcr::workbench::ModbusIoCommandReply &reply,
                           bool user_visible);

private:
  void emitResult(rcr::Result<rcr::workbench::ModbusIoSnapshot> snapshot,
                  const QString &peer, bool user_visible);
  void emitTransportFailure(const rcr::Error &error, const QString &peer);

  rcr::workbench::ModbusAgentClient client_{};
  QString last_host_{};
  quint16 last_port_{0};
};

class WorkbenchController final : public QObject {
  Q_OBJECT

public:
  WorkbenchController(rcr::workbench::RuntimeApplicationAdapter &adapter,
                      rcr::workbench::TestRunProvenance provenance,
                      std::string result_directory, QObject *parent = nullptr);
  // ThinkPad --cell-peer：无本地 RuntimeDaemon / CellReady 闭环。
  WorkbenchController(rcr::workbench::CellAppClient &cell_client,
                      rcr::workbench::TestRunProvenance provenance,
                      std::string result_directory, QObject *parent = nullptr);
  ~WorkbenchController() override;

  WorkbenchController(const WorkbenchController &) = delete;
  WorkbenchController &operator=(const WorkbenchController &) = delete;

  void
  setNextMockModbusWriteOutcome(rcr::workbench::ModbusIoCommandStatus outcome);

  void setRemoteHeartbeatRepliesEnabled(bool enabled);

public Q_SLOTS:
  void publishCurrentState();
  void startHealth();
  void cancelHealth();
  void activateRuntime();
  void commandServoHome();
  void commandServoTarget();
  void driveEnable();
  void driveDisable();
  void homeActuator();
  void startActuatorVelocity(double velocity_rad_s);
  void normalStopActuator();
  void quickStopActuator();
  void jogPressed(int direction, double velocity_rad_s);
  void jogReleased();
  void resetActuatorFault();
  void requestModbusScan();
  void setMockDigitalInput(int channel, bool active);
  void requestDigitalOutput(int channel, bool active);
  void requestAllOutputsOff();
  void selectLocalBackend();
  void selectRemoteLoopbackBackend();
  void connectRemoteLoopback();
  void disconnectRemoteLoopback();
  void selectMockModbusBackend();
  void selectPhysicalModbusBackend();
  void setModbusAgentPeer(const QString &peer);
  void disconnectPhysicalModbus();

Q_SIGNALS:
  void snapshotReady(const rcr::workbench::RuntimeTelemetrySnapshot &snapshot);
  void healthRequested(const QString &run_id);
  void healthStarted();
  void healthCompleted(const rcr::workbench::TestResult &result,
                       const QString &json_path, const QString &csv_path,
                       const QString &persistence_error);
  void actuatorSnapshotReady(const rcr::workbench::ActuatorSnapshot &snapshot);
  void
  actuatorCommandCompleted(const rcr::workbench::ActuatorCommandReply &reply);
  void modbusSnapshotReady(const rcr::workbench::ModbusIoSnapshot &snapshot);
  void
  modbusCommandCompleted(const rcr::workbench::ModbusIoCommandReply &reply);
  void remoteConnectionReady(
      const rcr::workbench::RemoteConnectionSnapshot &snapshot);
  void physicalModbusProbeRequested(const QString &host, quint16 port);
  void physicalModbusReadDiRequested();
  void physicalModbusWriteDoRequested(int channel, bool active);
  void physicalModbusAllOffRequested();

private:
  void publishSnapshot();
  void publishRemoteConnection();
  void tickRemoteLoopback();
  void tickActuator();
  void renewJog();
  void publishActuatorReply(const rcr::workbench::ActuatorCommandReply &reply);
  void publishModbusReply(const rcr::workbench::ModbusIoCommandReply &reply);
  void publishActiveModbusSnapshot();
  void resetPhysicalSnapshot();
  void tickPhysicalModbusPoll();
  void applyPhysicalTransaction(const rcr::workbench::ModbusIoSnapshot &snapshot,
                                const rcr::workbench::ModbusIoCommandReply &reply,
                                bool user_visible);
  void applyCellReadyOutput(const rcr::workbench::CellReadyDecision &decision);
  void submitServoBit(bool target_position);
  [[nodiscard]] bool physicalOutputsLive() const;
  [[nodiscard]] std::int64_t modbusNowNs() const;
  [[nodiscard]] bool parseAgentPeer(QString *host, quint16 *port) const;

  WorkbenchController(rcr::workbench::RuntimeApplicationAdapter *adapter,
                      rcr::workbench::CellAppClient *cell_client,
                      rcr::workbench::TestRunProvenance provenance,
                      std::string result_directory, QObject *parent);

  rcr::workbench::RuntimeApplicationAdapter *adapter_{nullptr};
  rcr::workbench::CellAppClient *cell_client_{nullptr};
  rcr::workbench::RuntimeTelemetrySnapshot last_runtime_snapshot_{};
  QTimer snapshot_timer_{};
  QTimer actuator_timer_{};
  QTimer jog_renew_timer_{};
  QTimer modbus_poll_timer_{};
  QElapsedTimer actuator_elapsed_{};
  QElapsedTimer modbus_elapsed_{};
  QElapsedTimer remote_elapsed_{};
  QThread worker_thread_{};
  HealthTestWorker *worker_{nullptr};
  QThread modbus_thread_{};
  ModbusAgentWorker *modbus_worker_{nullptr};
  rcr::workbench::MockActuatorProfile actuator_{};
  rcr::workbench::MockModbusIoProfile modbus_io_{};
  rcr::workbench::CellReadyMapper cell_ready_mapper_{};
  rcr::workbench::RemoteControlEndpoint remote_endpoint_{};
  rcr::workbench::RemoteRuntimeClient remote_client_{};
  rcr::workbench::RemoteBackendMode remote_mode_{
      rcr::workbench::RemoteBackendMode::Local};
  enum class ModbusBackend { Mock, Physical };
  ModbusBackend modbus_backend_{ModbusBackend::Mock};
  rcr::workbench::ModbusIoSnapshot physical_snapshot_{};
  QString modbus_agent_peer_{QStringLiteral("192.168.1.22:5740")};
  std::uint64_t active_jog_token_{0};
  std::uint16_t servo_command_sequence_{0};
  bool health_running_{false};
  bool modbus_request_running_{false};
  bool physical_command_blocked_{false};
};
