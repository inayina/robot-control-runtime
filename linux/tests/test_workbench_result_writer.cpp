#include "rcr/workbench/result_writer.hpp"
#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using rcr::workbench::CleanupStatus;
using rcr::workbench::CriterionResult;
using rcr::workbench::DiagnosticEvent;
using rcr::workbench::DiagnosticSeverity;
using rcr::workbench::DiagnosticSource;
using rcr::workbench::EvidenceClass;
using rcr::workbench::kResultSchemaId;
using rcr::workbench::Measurement;
using rcr::workbench::MeasurementQuality;
using rcr::workbench::ResultWriter;
using rcr::workbench::TestOutcome;
using rcr::workbench::TestResult;

class TempDir {
public:
  TempDir() {
    auto pattern =
        (std::filesystem::temp_directory_path() / "rcr-rw-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) == nullptr) {
      throw rcr::test::Failure("mkdtemp failed");
    }
    path_ = buffer.data();
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_{};
};

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TestResult complete_failure() {
  TestResult result{};
  result.run_id = "run-fail-001";
  result.case_id = "can.communication_health";
  result.case_name = "CAN Communication Health";
  result.case_version = "1";
  result.outcome = TestOutcome::Failed;
  result.started_ns = 100;
  result.finished_ns = 200;
  result.started_wall_ns = 1'700'000'000'000'000'000;
  result.finished_wall_ns = 1'700'000'000'100'000'000;
  result.reason = "heartbeat too old";
  result.summary = "CAN communication health criteria failed";
  result.cleanup_status = CleanupStatus::Passed;
  result.environment.backend = "SOCKETCAN";
  result.environment.interface_name = "vcan0";
  result.environment.dut_id = "CAN_NODE_1";
  result.environment.profile = "can.communication_health";
  result.environment.evidence = EvidenceClass::Vcan;
  result.provenance.git_commit = "deadbeef";
  result.provenance.git_dirty = true;
  result.provenance.build_type = "Debug";
  result.parameters.push_back({"observation_window_ms", "100"});
  result.measurements.push_back(Measurement{
      "max_heartbeat_age", "ms", 80.0, 200, MeasurementQuality::Simulated});
  result.criteria.push_back(
      CriterionResult{"heartbeat freshness", false, "0..40 ms", "80 ms"});
  DiagnosticEvent event{};
  event.observed_monotonic_ns = 200;
  event.source = DiagnosticSource::Communication;
  event.severity = DiagnosticSeverity::Error;
  event.code = "HEARTBEAT_STALE";
  event.message = "heartbeat age exceeded freshness threshold";
  event.device_id = "CAN_NODE_1";
  event.run_id = "run-fail-001";
  event.context = "criterion=heartbeat freshness";
  result.diagnostics.push_back(std::move(event));
  return result;
}

} // namespace

RCR_TEST(JsonSchemaContainsFrozenEvidenceFields) {
  const auto json = rcr::workbench::serialize_result_json(complete_failure());

  RCR_EXPECT(json.find("\"schema\": \"rcr.workbench.result.v1\"") !=
             std::string::npos);
  RCR_EXPECT(json.find("\"run_id\": \"run-fail-001\"") != std::string::npos);
  RCR_EXPECT(json.find("\"outcome\": \"FAIL\"") != std::string::npos);
  RCR_EXPECT(json.find("\"reason\": \"heartbeat too old\"") !=
             std::string::npos);
  RCR_EXPECT(json.find("\"evidence\": \"VCAN\"") != std::string::npos);
  RCR_EXPECT(json.find("\"git_commit\": \"deadbeef\"") != std::string::npos);
  RCR_EXPECT(json.find("\"status\": \"PASSED\"") != std::string::npos);
  RCR_EXPECT(json.find("\"code\": \"HEARTBEAT_STALE\"") != std::string::npos);
  RCR_EXPECT(json.find("heartbeat freshness") != std::string::npos);
  RCR_EXPECT(json.find("max_heartbeat_age") != std::string::npos);
}

RCR_TEST(CsvIsSingleIndexRowWithEscapedReason) {
  auto result = complete_failure();
  result.reason = "heartbeat too old, still latched";
  const auto csv = rcr::workbench::serialize_result_csv(result);

  RCR_EXPECT(csv.find(std::string{kResultSchemaId}) != std::string::npos);
  RCR_EXPECT(csv.find("\"heartbeat too old, still latched\"") !=
             std::string::npos);
  RCR_EXPECT(csv.find("FAIL") != std::string::npos);
  std::size_t lines = 0;
  for (char ch : csv) {
    if (ch == '\n') {
      ++lines;
    }
  }
  RCR_EXPECT(lines == 2);
}

RCR_TEST(FailWithoutReasonIsRejectedAndLeavesNoFinalFile) {
  auto result = complete_failure();
  result.reason.clear();
  TempDir dir;
  ResultWriter writer;

  const auto written = writer.write(result, dir.path());

  RCR_EXPECT(!written.ok());
  RCR_EXPECT(written.error().code() == rcr::Errc::InvalidArgument);
  RCR_EXPECT(!std::filesystem::exists(dir.path() / "run-fail-001.json"));
  RCR_EXPECT(!std::filesystem::exists(dir.path() / "run-fail-001.csv"));
}

RCR_TEST(UnsafeRunIdIsRejected) {
  auto result = complete_failure();
  result.run_id = "../escape";
  RCR_EXPECT(!ResultWriter::validate_persistable(result).ok());
}

RCR_TEST(AtomicWriteReplacesLeftoverTmpAndRefusesOverwrite) {
  auto result = complete_failure();
  TempDir dir;
  const auto json_path = dir.path() / "run-fail-001.json";
  const auto tmp_path = dir.path() / "run-fail-001.json.tmp";
  {
    std::ofstream leftover(tmp_path);
    leftover << "partial-garbage";
  }

  ResultWriter writer;
  const auto first = writer.write(result, dir.path());
  RCR_REQUIRE(first.ok());
  RCR_EXPECT(!std::filesystem::exists(tmp_path));
  const auto body = read_file(first.value().json_path);
  RCR_EXPECT(body.find("partial-garbage") == std::string::npos);
  RCR_EXPECT(body.find("\"schema\": \"rcr.workbench.result.v1\"") !=
             std::string::npos);

  const auto second = writer.write(result, dir.path());
  RCR_EXPECT(!second.ok());
  RCR_EXPECT(second.error().code() == rcr::Errc::Busy);
  RCR_EXPECT(read_file(json_path) == body);
}

RCR_TEST_MAIN()
