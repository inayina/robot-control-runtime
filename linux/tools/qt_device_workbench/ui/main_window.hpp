#pragma once

#include "controller/qt_metatypes.hpp"

#include <QMainWindow>

class QLabel;
class QDoubleSpinBox;
class QPushButton;
class QTableWidget;
class WorkbenchController;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(WorkbenchController &controller,
                      QWidget *parent = nullptr);

private Q_SLOTS:
  void updateSnapshot(const rcr::workbench::RuntimeTelemetrySnapshot &snapshot);
  void showHealthStarted();
  void showHealthResult(const rcr::workbench::TestResult &result,
                        const QString &json_path, const QString &csv_path,
                        const QString &persistence_error);
  void updateActuator(const rcr::workbench::ActuatorSnapshot &snapshot);
  void showActuatorReply(const rcr::workbench::ActuatorCommandReply &reply);

private:
  QWidget *makeOverviewPage();
  QWidget *makeTestsPage();
  QWidget *makeDiagnosticsPage();
  QWidget *makeResultsPage();
  QWidget *makeActuatorPage();

  WorkbenchController &controller_;
  QLabel *runtime_state_{nullptr};
  QLabel *backend_{nullptr};
  QLabel *scheduler_{nullptr};
  QLabel *device_{nullptr};
  QLabel *heartbeat_{nullptr};
  QLabel *test_outcome_{nullptr};
  QLabel *result_paths_{nullptr};
  QPushButton *run_health_{nullptr};
  QPushButton *cancel_health_{nullptr};
  QTableWidget *criteria_{nullptr};
  QTableWidget *diagnostics_{nullptr};
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
};
