#include "main_window.hpp"

#include "workbench_controller.hpp"

#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace {

QString text(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString text(const std::string &value) { return QString::fromStdString(value); }

QTableWidgetItem *item(const QString &value) {
  return new QTableWidgetItem(value);
}

} // namespace

MainWindow::MainWindow(WorkbenchController &controller, QWidget *parent)
    : QMainWindow(parent), controller_(controller) {
  setWindowTitle(QStringLiteral("Robot Device Test & Diagnostic Workbench"));
  resize(920, 620);

  auto *tabs = new QTabWidget(this);
  tabs->addTab(makeOverviewPage(), QStringLiteral("Overview"));
  tabs->addTab(makeTestsPage(), QStringLiteral("Tests"));
  tabs->addTab(makeDiagnosticsPage(), QStringLiteral("Diagnostics"));
  tabs->addTab(makeResultsPage(), QStringLiteral("Results"));
  setCentralWidget(tabs);

  connect(&controller_, &WorkbenchController::snapshotReady, this,
          &MainWindow::updateSnapshot);
  connect(&controller_, &WorkbenchController::healthStarted, this,
          &MainWindow::showHealthStarted);
  connect(&controller_, &WorkbenchController::healthCompleted, this,
          &MainWindow::showHealthResult);
}

QWidget *MainWindow::makeOverviewPage() {
  auto *page = new QWidget(this);
  auto *form = new QFormLayout(page);
  runtime_state_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  backend_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  scheduler_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  device_ = new QLabel(QStringLiteral("UNKNOWN"), page);
  heartbeat_ = new QLabel(QStringLiteral("N/A"), page);
  form->addRow(QStringLiteral("Runtime"), runtime_state_);
  form->addRow(QStringLiteral("Backend / Evidence"), backend_);
  form->addRow(QStringLiteral("Scheduler"), scheduler_);
  form->addRow(QStringLiteral("Device"), device_);
  form->addRow(QStringLiteral("Heartbeat age"), heartbeat_);
  return page;
}

QWidget *MainWindow::makeTestsPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  run_health_ = new QPushButton(QStringLiteral("Run CAN Health"), page);
  cancel_health_ = new QPushButton(QStringLiteral("Cancel"), page);
  cancel_health_->setEnabled(false);
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
  result_paths_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  result_paths_->setWordWrap(true);
  layout->addWidget(result_paths_);
  layout->addStretch();
  return page;
}

void MainWindow::updateSnapshot(
    const rcr::workbench::RuntimeTelemetrySnapshot &snapshot) {
  runtime_state_->setText(
      text(rcr::workbench::to_string(snapshot.runtime.mode)));
  backend_->setText(
      text(snapshot.communication.backend) + QStringLiteral(" / ") +
      text(rcr::workbench::to_string(snapshot.communication.evidence)));
  scheduler_->setText(snapshot.runtime.scheduler_running
                          ? QStringLiteral("RUNNING")
                          : QStringLiteral("STOPPED"));
  device_->setText(text(snapshot.device.device_id) + QStringLiteral(" / ") +
                   (snapshot.device.online ? QStringLiteral("ONLINE")
                                           : QStringLiteral("OFFLINE")));
  heartbeat_->setText(snapshot.device.heartbeat_age_ns < 0
                          ? QStringLiteral("N/A")
                          : QStringLiteral("%1 ms").arg(
                                snapshot.device.heartbeat_age_ns / 1'000'000));
}

void MainWindow::showHealthStarted() {
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
