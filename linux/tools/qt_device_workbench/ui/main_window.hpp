#pragma once

// 只展示、只发请求。不打开 CAN、不做 PASS/FAIL、不拥有 RuntimeDaemon。
// Connection 页同样不拥有 socket；只发 Local/Remote/Connect 请求并显示 DTO。
//
// Q_OBJECT：让 moc 生成 signal/slot 元数据。没有它，下面的 connect 编不过。
// controller_ 是引用不是孩子：窗口关掉不能把 Controller / worker / daemon
// 一起删掉。 控件指针由 Qt 父子树释放（new QLabel(page) 的 page 是
// parent），不必在析构里 delete。
//
// 对照笔记：docs/workbench/NOTES.md §7.3。

#include "controller/qt_metatypes.hpp"

#include <QMainWindow>

#include <array>

class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class WorkbenchController;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(WorkbenchController &controller,
                      QWidget *parent = nullptr);

private Q_SLOTS:
  // 这些 slot 只改文字/表格/按钮灰显。频率和状态机在 Controller / headless
  // 服务里。
  void updateSnapshot(const rcr::workbench::RuntimeTelemetrySnapshot &snapshot);
  void showHealthStarted();
  void showHealthResult(const rcr::workbench::TestResult &result,
                        const QString &json_path, const QString &csv_path,
                        const QString &persistence_error);
  void updateActuator(const rcr::workbench::ActuatorSnapshot &snapshot);
  void showActuatorReply(const rcr::workbench::ActuatorCommandReply &reply);
  void updateModbus(const rcr::workbench::ModbusIoSnapshot &snapshot);
  void showModbusReply(const rcr::workbench::ModbusIoCommandReply &reply);
  void updateRemoteConnection(
      const rcr::workbench::RemoteConnectionSnapshot &snapshot);

private:
  QWidget *makeOverviewPage();
  QWidget *makeRuntimePage();
  QWidget *makeConnectionPage();
  QWidget *makeTestsPage();
  QWidget *makeDiagnosticsPage();
  QWidget *makeResultsPage();
  QWidget *makeVerificationPage();
  QWidget *makeActuatorPage();
  QWidget *makeModbusPage();

  WorkbenchController &controller_;
  QLabel *overview_runtime_{nullptr};
  QLabel *overview_node_{nullptr};
  QLabel *overview_position_reached_{nullptr};
  QLabel *overview_cell_ready_{nullptr};
  QLabel *overview_do0_requested_{nullptr};
  QLabel *overview_do0_confirmed_{nullptr};
  QLabel *runtime_state_{nullptr};
  QLabel *runtime_fault_{nullptr};
  QLabel *interlock_{nullptr};
  QLabel *backend_{nullptr};
  QLabel *interface_{nullptr};
  QLabel *scheduler_{nullptr};
  QLabel *device_{nullptr};
  QLabel *heartbeat_{nullptr};
  QLabel *can_traffic_{nullptr};
  QLabel *can_rejects_{nullptr};
  QLabel *device_session_{nullptr};
  QLabel *output_ack_{nullptr};
  QLabel *test_outcome_{nullptr};
  QLabel *result_paths_{nullptr};
  QPushButton *run_health_{nullptr};
  QPushButton *cancel_health_{nullptr};
  QPushButton *activate_runtime_{nullptr};
  QPushButton *command_home_{nullptr};
  QPushButton *command_target_{nullptr};
  QTableWidget *criteria_{nullptr};
  QTableWidget *diagnostics_{nullptr};
  QLabel *remote_banner_{nullptr};
  QLabel *remote_mode_{nullptr};
  QLabel *remote_peer_{nullptr};
  QLabel *remote_session_{nullptr};
  QLabel *remote_heartbeat_{nullptr};
  QLabel *remote_status_count_{nullptr};
  QLabel *remote_last_error_{nullptr};
  QLabel *remote_status_mode_{nullptr};
  QPushButton *remote_select_local_{nullptr};
  QPushButton *remote_select_loopback_{nullptr};
  QPushButton *remote_connect_{nullptr};
  QPushButton *remote_disconnect_{nullptr};
  QLabel *actuator_state_{nullptr};
  QLabel *actuator_mode_{nullptr};
  QLabel *actuator_enabled_{nullptr};
  QLabel *actuator_homed_{nullptr};
  QLabel *actuator_position_{nullptr};
  QLabel *actuator_velocity_{nullptr};
  QLabel *actuator_limits_{nullptr};
  QLabel *actuator_fault_{nullptr};
  QLabel *actuator_reply_{nullptr};
  QDoubleSpinBox *actuator_velocity_input_{nullptr};
  QDoubleSpinBox *jog_velocity_input_{nullptr};
  QPushButton *drive_enable_{nullptr};
  QPushButton *drive_disable_{nullptr};
  QPushButton *home_actuator_{nullptr};
  QPushButton *start_actuator_{nullptr};
  QPushButton *normal_stop_actuator_{nullptr};
  QPushButton *quick_stop_actuator_{nullptr};
  QPushButton *jog_negative_{nullptr};
  QPushButton *jog_positive_{nullptr};
  QPushButton *reset_actuator_fault_{nullptr};
  QLabel *modbus_backend_{nullptr};
  QLabel *modbus_evidence_{nullptr};
  QLabel *modbus_transport_{nullptr};
  QLabel *modbus_serial_port_{nullptr};
  QLabel *modbus_baud_{nullptr};
  QLabel *modbus_parity_{nullptr};
  QLabel *modbus_slave_{nullptr};
  QLabel *modbus_status_{nullptr};
  QLabel *cell_ready_value_{nullptr};
  QLabel *modbus_sku_{nullptr};
  QLabel *modbus_rtt_{nullptr};
  QLabel *modbus_scan_summary_{nullptr};
  QLabel *modbus_reply_{nullptr};
  QLineEdit *modbus_agent_peer_{nullptr};
  QPushButton *modbus_select_mock_{nullptr};
  QPushButton *modbus_select_physical_{nullptr};
  QPushButton *modbus_scan_{nullptr};
  QPushButton *modbus_disconnect_{nullptr};
  QPushButton *modbus_all_off_{nullptr};
  std::array<QLabel *, rcr::workbench::kModbusIoChannelCount>
      modbus_di_values_{};
  std::array<QCheckBox *, rcr::workbench::kModbusIoChannelCount>
      modbus_di_injections_{};
  std::array<QCheckBox *, rcr::workbench::kModbusIoChannelCount>
      modbus_do_requests_{};
  std::array<QLabel *, rcr::workbench::kModbusIoChannelCount>
      modbus_do_requested_{};
  std::array<QLabel *, rcr::workbench::kModbusIoChannelCount>
      modbus_do_confirmed_{};
  std::array<QLabel *, rcr::workbench::kModbusIoChannelCount>
      modbus_do_status_{};
};
