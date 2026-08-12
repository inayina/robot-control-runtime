#pragma once

// 只展示、只发请求。不打开 CAN、不做 PASS/FAIL、不拥有 RuntimeDaemon。
//
// Q_OBJECT：让 moc 生成 signal/slot 元数据。没有它，下面的 connect 编不过。
// controller_ 是引用不是孩子：窗口关掉不能把 Controller / worker / daemon 一起删掉。
// 控件指针由 Qt 父子树释放（new QLabel(page) 的 page 是 parent），不必在析构里 delete。
//
// 对照笔记：docs/workbench/NOTES.md §7.3。

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
  // 这些 slot 只改文字/表格/按钮灰显。阈值和状态机在 Controller / headless 服务里。
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
