#pragma once

// Workbench 应用层：把内存 TestResult 写成可复核文件。它不拥有 Runtime、CAN
// fd 或测试生命周期，也不把写入失败升级为 Runtime fault。

#include "rcr/result.hpp"
#include "rcr/workbench/test_runner.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace rcr::workbench {

inline constexpr std::string_view kResultSchemaId = "rcr.workbench.result.v1";

struct ResultArtifacts {
  std::string json_path{};
  std::string csv_path{};
};

/// 将一次测试结果写成同目录 JSON + 扁平 CSV。
///
/// JSON 是完整证据；CSV 只做表格索引。最终文件通过同目录临时文件 + rename
/// 出现，避免崩溃留下看似完整的半份结果。已存在的最终文件拒绝覆盖。
class ResultWriter {
public:
  [[nodiscard]] Result<ResultArtifacts>
  write(const TestResult &result, const std::filesystem::path &directory) const;

  [[nodiscard]] static Result<void>
  validate_persistable(const TestResult &result);
};

[[nodiscard]] std::string serialize_result_json(const TestResult &result);
[[nodiscard]] std::string serialize_result_csv(const TestResult &result);

} // namespace rcr::workbench
