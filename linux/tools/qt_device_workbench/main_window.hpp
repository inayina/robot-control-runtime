#pragma once

#include "qt_metatypes.hpp"

#include <QMainWindow>

class QLabel;
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

private:
  QWidget *makeOverviewPage();
  QWidget *makeTestsPage();
  QWidget *makeDiagnosticsPage();
  QWidget *makeResultsPage();

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
};
