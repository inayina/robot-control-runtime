#pragma once

#include "qt_metatypes.hpp"

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
  void startHealth();
  void cancelHealth();

Q_SIGNALS:
  void snapshotReady(const rcr::workbench::RuntimeTelemetrySnapshot &snapshot);
  void healthRequested(const QString &run_id);
  void healthStarted();
  void healthCompleted(const rcr::workbench::TestResult &result,
                       const QString &json_path, const QString &csv_path,
                       const QString &persistence_error);

private:
  void publishSnapshot();

  rcr::workbench::RuntimeApplicationAdapter &adapter_;
  QTimer snapshot_timer_{};
  QThread worker_thread_{};
  HealthTestWorker *worker_{nullptr};
  bool health_running_{false};
};
