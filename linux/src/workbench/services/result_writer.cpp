#include "rcr/workbench/services/result_writer.hpp"

#include "rcr/owned_fd.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>

namespace rcr::workbench {
namespace {

bool is_safe_run_id(std::string_view run_id) noexcept {
  if (run_id.empty() || run_id == "." || run_id == "..") {
    return false;
  }
  for (const unsigned char ch : run_id) {
    if (!(std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-')) {
      return false;
    }
  }
  return true;
}

std::size_t failed_criteria_count(const TestResult &result) {
  std::size_t failed = 0;
  for (const auto &item : result.criteria) {
    if (!item.passed) {
      ++failed;
    }
  }
  return failed;
}

void append_indent(std::string &out, int depth) {
  out.append(static_cast<std::size_t>(depth) * 2, ' ');
}

void append_escaped(std::string &out, std::string_view value) {
  out.push_back('"');
  for (const unsigned char ch : value) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (ch < 0x20) {
        std::ostringstream hex;
        hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(ch);
        out += hex.str();
      } else {
        out.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  out.push_back('"');
}

void append_number(std::string &out, double value) {
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }
  std::ostringstream stream;
  stream << std::setprecision(15) << value;
  out += stream.str();
}

void append_key(std::string &out, int depth, bool &first,
                std::string_view key) {
  if (!first) {
    out += ",\n";
  }
  first = false;
  append_indent(out, depth);
  append_escaped(out, key);
  out += ": ";
}

void append_string_field(std::string &out, int depth, bool &first,
                         std::string_view key, std::string_view value) {
  append_key(out, depth, first, key);
  append_escaped(out, value);
}

void append_int_field(std::string &out, int depth, bool &first,
                      std::string_view key, std::int64_t value) {
  append_key(out, depth, first, key);
  out += std::to_string(value);
}

void append_bool_field(std::string &out, int depth, bool &first,
                       std::string_view key, bool value) {
  append_key(out, depth, first, key);
  out += value ? "true" : "false";
}

void append_csv_field(std::string &out, std::string_view value, bool last) {
  const bool quote = value.find_first_of(",\"\n\r") != std::string_view::npos;
  if (quote) {
    out.push_back('"');
    for (const char ch : value) {
      if (ch == '"') {
        out += "\"\"";
      } else {
        out.push_back(ch);
      }
    }
    out.push_back('"');
  } else {
    out.append(value);
  }
  if (!last) {
    out.push_back(',');
  }
}

Result<void> write_file_atomically(const std::filesystem::path &final_path,
                                   std::string_view contents) {
  if (std::filesystem::exists(final_path)) {
    return Error{Errc::Busy, "refuse to overwrite existing result file: " +
                                 final_path.string()};
  }

  auto tmp_path = final_path;
  tmp_path += ".tmp";
  std::error_code remove_error;
  std::filesystem::remove(tmp_path, remove_error);

  // 同目录临时文件 + rename：读者要么看到旧完整文件，要么看到新完整文件。
  // 不 fsync 目录，因为这是本地测试证据，不是崩溃恢复数据库。
  OwnedFd fd{::open(tmp_path.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_TRUNC, 0644)};
  if (!fd) {
    return Error{Errc::IoError,
                 std::string("open result tmp: ") + std::strerror(errno)};
  }

  std::size_t offset = 0;
  while (offset < contents.size()) {
    const ssize_t written =
        ::write(fd.get(), contents.data() + offset, contents.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      const int write_errno = errno;
      fd.reset();
      std::filesystem::remove(tmp_path, remove_error);
      return Error{Errc::IoError, std::string("write result tmp: ") +
                                      std::strerror(write_errno)};
    }
    offset += static_cast<std::size_t>(written);
  }

  if (::fsync(fd.get()) != 0) {
    const int sync_errno = errno;
    fd.reset();
    std::filesystem::remove(tmp_path, remove_error);
    return Error{Errc::IoError,
                 std::string("fsync result tmp: ") + std::strerror(sync_errno)};
  }
  fd.reset();

  if (::rename(tmp_path.c_str(), final_path.c_str()) != 0) {
    const int rename_errno = errno;
    std::filesystem::remove(tmp_path, remove_error);
    return Error{Errc::IoError, std::string("rename result file: ") +
                                    std::strerror(rename_errno)};
  }
  return Result<void>::success();
}

} // namespace

Result<void> ResultWriter::validate_persistable(const TestResult &result) {
  if (!is_safe_run_id(result.run_id)) {
    return Error{Errc::InvalidArgument,
                 "run_id must be a portable file stem: [A-Za-z0-9._-]+"};
  }
  if (result.case_id.empty() || result.case_name.empty()) {
    return Error{Errc::InvalidArgument,
                 "case id and case name must be non-empty"};
  }
  if (result.outcome != TestOutcome::Failed &&
      result.outcome != TestOutcome::Error) {
    return Result<void>::success();
  }
  if (result.reason.empty()) {
    return Error{Errc::InvalidArgument,
                 "FAIL/ERROR result must include a reason"};
  }
  if (result.criteria.empty() || result.measurements.empty() ||
      result.diagnostics.empty()) {
    return Error{Errc::InvalidArgument,
                 "FAIL/ERROR result must include criteria, measurement and "
                 "diagnostic evidence"};
  }
  return Result<void>::success();
}

std::string serialize_result_json(const TestResult &result) {
  std::string out;
  out += "{\n";
  bool first = true;
  append_string_field(out, 1, first, "schema", kResultSchemaId);
  append_string_field(out, 1, first, "run_id", result.run_id);

  append_key(out, 1, first, "case");
  out += "{\n";
  bool case_first = true;
  append_string_field(out, 2, case_first, "id", result.case_id);
  append_string_field(out, 2, case_first, "name", result.case_name);
  append_string_field(out, 2, case_first, "version", result.case_version);
  out += "\n";
  append_indent(out, 1);
  out += "}";

  append_string_field(out, 1, first, "outcome", to_string(result.outcome));
  append_string_field(out, 1, first, "reason", result.reason);
  append_string_field(out, 1, first, "summary", result.summary);

  append_key(out, 1, first, "time");
  out += "{\n";
  bool time_first = true;
  append_int_field(out, 2, time_first, "started_monotonic_ns",
                   result.started_ns);
  append_int_field(out, 2, time_first, "finished_monotonic_ns",
                   result.finished_ns);
  append_int_field(out, 2, time_first, "started_wall_ns",
                   result.started_wall_ns);
  append_int_field(out, 2, time_first, "finished_wall_ns",
                   result.finished_wall_ns);
  out += "\n";
  append_indent(out, 1);
  out += "}";

  append_key(out, 1, first, "provenance");
  out += "{\n";
  bool provenance_first = true;
  append_string_field(out, 2, provenance_first, "git_commit",
                      result.provenance.git_commit);
  append_bool_field(out, 2, provenance_first, "git_dirty",
                    result.provenance.git_dirty);
  append_string_field(out, 2, provenance_first, "build_type",
                      result.provenance.build_type);
  out += "\n";
  append_indent(out, 1);
  out += "}";

  append_key(out, 1, first, "environment");
  out += "{\n";
  bool env_first = true;
  append_string_field(out, 2, env_first, "backend", result.environment.backend);
  append_string_field(out, 2, env_first, "interface",
                      result.environment.interface_name);
  append_string_field(out, 2, env_first, "dut_id", result.environment.dut_id);
  append_string_field(out, 2, env_first, "profile", result.environment.profile);
  append_string_field(out, 2, env_first, "evidence",
                      to_string(result.environment.evidence));
  out += "\n";
  append_indent(out, 1);
  out += "}";

  append_key(out, 1, first, "parameters");
  out += "[\n";
  for (std::size_t i = 0; i < result.parameters.size(); ++i) {
    append_indent(out, 2);
    out += "{\n";
    bool item_first = true;
    append_string_field(out, 3, item_first, "name", result.parameters[i].name);
    append_string_field(out, 3, item_first, "value",
                        result.parameters[i].value);
    out += "\n";
    append_indent(out, 2);
    out += i + 1 == result.parameters.size() ? "}" : "},";
    out += "\n";
  }
  append_indent(out, 1);
  out += "]";

  append_key(out, 1, first, "error");
  out += "{\n";
  bool error_first = true;
  append_string_field(out, 2, error_first, "code",
                      rcr::to_string(result.error.code()));
  append_string_field(out, 2, error_first, "message", result.error.message());
  out += "\n";
  append_indent(out, 1);
  out += "}";

  append_key(out, 1, first, "cleanup");
  out += "{\n";
  bool cleanup_first = true;
  append_string_field(out, 2, cleanup_first, "status",
                      to_string(result.cleanup_status));
  append_string_field(out, 2, cleanup_first, "error_code",
                      rcr::to_string(result.cleanup_error.code()));
  append_string_field(out, 2, cleanup_first, "error_message",
                      result.cleanup_error.message());
  out += "\n";
  append_indent(out, 1);
  out += "}";

  append_key(out, 1, first, "criteria");
  out += "[\n";
  for (std::size_t i = 0; i < result.criteria.size(); ++i) {
    append_indent(out, 2);
    out += "{\n";
    bool item_first = true;
    append_string_field(out, 3, item_first, "name", result.criteria[i].name);
    append_bool_field(out, 3, item_first, "passed", result.criteria[i].passed);
    append_string_field(out, 3, item_first, "expected",
                        result.criteria[i].expected);
    append_string_field(out, 3, item_first, "actual",
                        result.criteria[i].actual);
    out += "\n";
    append_indent(out, 2);
    out += i + 1 == result.criteria.size() ? "}" : "},";
    out += "\n";
  }
  append_indent(out, 1);
  out += "]";

  append_key(out, 1, first, "measurements");
  out += "[\n";
  for (std::size_t i = 0; i < result.measurements.size(); ++i) {
    append_indent(out, 2);
    out += "{\n";
    bool item_first = true;
    append_string_field(out, 3, item_first, "name",
                        result.measurements[i].name);
    append_string_field(out, 3, item_first, "unit",
                        result.measurements[i].unit);
    append_key(out, 3, item_first, "value");
    append_number(out, result.measurements[i].value);
    append_int_field(out, 3, item_first, "monotonic_ns",
                     result.measurements[i].monotonic_ns);
    append_string_field(out, 3, item_first, "quality",
                        to_string(result.measurements[i].quality));
    out += "\n";
    append_indent(out, 2);
    out += i + 1 == result.measurements.size() ? "}" : "},";
    out += "\n";
  }
  append_indent(out, 1);
  out += "]";

  append_key(out, 1, first, "diagnostics");
  out += "[\n";
  for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
    append_indent(out, 2);
    out += "{\n";
    bool item_first = true;
    append_int_field(out, 3, item_first, "observed_monotonic_ns",
                     result.diagnostics[i].observed_monotonic_ns);
    append_string_field(out, 3, item_first, "source",
                        to_string(result.diagnostics[i].source));
    append_string_field(out, 3, item_first, "severity",
                        to_string(result.diagnostics[i].severity));
    append_string_field(out, 3, item_first, "code", result.diagnostics[i].code);
    append_string_field(out, 3, item_first, "message",
                        result.diagnostics[i].message);
    append_string_field(out, 3, item_first, "device_id",
                        result.diagnostics[i].device_id);
    append_string_field(out, 3, item_first, "run_id",
                        result.diagnostics[i].run_id);
    append_string_field(out, 3, item_first, "context",
                        result.diagnostics[i].context);
    out += "\n";
    append_indent(out, 2);
    out += i + 1 == result.diagnostics.size() ? "}" : "},";
    out += "\n";
  }
  append_indent(out, 1);
  out += "]";

  out += "\n}\n";
  return out;
}

std::string serialize_result_csv(const TestResult &result) {
  std::string out;
  out += "schema,run_id,case_id,case_name,case_version,outcome,reason,"
         "evidence,backend,interface,dut_id,cleanup_status,error_code,"
         "started_monotonic_ns,finished_monotonic_ns,started_wall_ns,"
         "finished_wall_ns,git_commit,git_dirty,measurement_count,"
         "failed_criteria_count,diagnostic_count,summary\n";

  const auto field = [&out](std::string_view value, bool last = false) {
    append_csv_field(out, value, last);
  };
  field(kResultSchemaId);
  field(result.run_id);
  field(result.case_id);
  field(result.case_name);
  field(result.case_version);
  field(to_string(result.outcome));
  field(result.reason);
  field(to_string(result.environment.evidence));
  field(result.environment.backend);
  field(result.environment.interface_name);
  field(result.environment.dut_id);
  field(to_string(result.cleanup_status));
  field(rcr::to_string(result.error.code()));
  field(std::to_string(result.started_ns));
  field(std::to_string(result.finished_ns));
  field(std::to_string(result.started_wall_ns));
  field(std::to_string(result.finished_wall_ns));
  field(result.provenance.git_commit);
  field(result.provenance.git_dirty ? "true" : "false");
  field(std::to_string(result.measurements.size()));
  field(std::to_string(failed_criteria_count(result)));
  field(std::to_string(result.diagnostics.size()));
  field(result.summary, true);
  out += '\n';
  return out;
}

Result<ResultArtifacts>
ResultWriter::write(const TestResult &result,
                    const std::filesystem::path &directory) const {
  const auto valid = validate_persistable(result);
  if (!valid) {
    return valid.error();
  }
  if (directory.empty()) {
    return Error{Errc::InvalidArgument, "result directory must be non-empty"};
  }

  std::error_code create_error;
  std::filesystem::create_directories(directory, create_error);
  if (create_error) {
    return Error{Errc::IoError,
                 "create result directory: " + create_error.message()};
  }

  ResultArtifacts artifacts{};
  const auto json_path = directory / (result.run_id + ".json");
  const auto csv_path = directory / (result.run_id + ".csv");
  const auto json_written =
      write_file_atomically(json_path, serialize_result_json(result));
  if (!json_written) {
    return json_written.error();
  }
  const auto csv_written =
      write_file_atomically(csv_path, serialize_result_csv(result));
  if (!csv_written) {
    std::error_code remove_error;
    std::filesystem::remove(json_path, remove_error);
    return csv_written.error();
  }

  artifacts.json_path = json_path.string();
  artifacts.csv_path = csv_path.string();
  return artifacts;
}

} // namespace rcr::workbench
