#pragma once

// Workbench 用例层：拉快照、跑测试、推进隔离 Mock。不画控件，也不 start/stop daemon。
//
// 两个线程：
//   UI 线程   — 本对象、三个 QTimer、Mock tick；只做快读和发请求。
//   worker 线程 — HealthTestWorker，跑同步 CAN Health + fsync 写文件。
// Cancel 不能 queued 到 worker：run() 占着那边的 event loop，排队过去等于测完才看见。
//
// CMake 开了 QT_NO_KEYWORDS，所以写 Q_SLOTS / Q_SIGNALS / Q_EMIT，不写 slots/signals/emit。
// 后者是宏，会污染 Runtime 头里的普通参数名（例如 CanIoLoop 的 signals）。
//
// 对照笔记：docs/workbench/NOTES.md §5、§7.2。

#include "controller/qt_metatypes.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <string>

namespace rcr::workbench {
class RuntimeApplicationAdapter;
}

// 活在 worker 线程里的 QObject。禁止在这里保存 QLabel* 或直接 setText。
class HealthTestWorker final : public QObject {
  Q_OBJECT

public:
  HealthTestWorker(rcr::workbench::RuntimeApplicationAdapter &adapter,
                   rcr::workbench::TestRunProvenance provenance,
                   std::string result_directory);

  // TestRunner 的取消入口是跨线程安全的；不能用 queued slot，因为 run() 执行时
  // worker event loop 正被同步测试占用，queued cancel 只能等测试结束后才到达。
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

class WorkbenchController final : public QObject {
  Q_OBJECT

public:
  WorkbenchController(rcr::workbench::RuntimeApplicationAdapter &adapter,
                      rcr::workbench::TestRunProvenance provenance,
                      std::string result_directory, QObject *parent = nullptr);
  ~WorkbenchController() override;

  WorkbenchController(const WorkbenchController &) = delete;
  WorkbenchController &operator=(const WorkbenchController &) = delete;

public Q_SLOTS:
  // 全部在 UI 线程被按钮点到。Health 只投递到 worker；Actuator 走进程内 Mock。
  void startHealth();
  void cancelHealth();
  void driveEnable();
  void driveDisable();
  void homeActuator();
  void startActuatorVelocity(double velocity_rad_s);
  void normalStopActuator();
  void quickStopActuator();
  void jogPressed(int direction, double velocity_rad_s);
  void jogReleased();
  void resetActuatorFault();

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

private:
  void publishSnapshot();
  void tickActuator();
  void renewJog();
  void publishActuatorReply(const rcr::workbench::ActuatorCommandReply &reply);

  rcr::workbench::RuntimeApplicationAdapter &adapter_;
  // 100 ms 刷新显示；10 ms 推 Mock；50 ms 续 Jog lease。都不是 Runtime 控制周期。
  QTimer snapshot_timer_{};
  QTimer actuator_timer_{};
  QTimer jog_renew_timer_{};
  QElapsedTimer actuator_elapsed_{};
  QThread worker_thread_{};
  HealthTestWorker *worker_{nullptr};
  // 隔离 Mock：不进 Runtime，不占 CAN fd。UI 崩了不应被理解成电机还在转。
  rcr::workbench::MockActuatorProfile actuator_{};
  std::uint64_t active_jog_token_{0};
  bool health_running_{false};
};
