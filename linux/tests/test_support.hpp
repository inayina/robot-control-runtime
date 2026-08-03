#pragma once

// Linux Runtime 使用的零依赖测试辅助，不作为固件测试框架共享。

#include <cstdlib>
#include <dirent.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <unistd.h>

namespace rcr::test {

/// 统计 `/proc/<pid>/fd` 下的打开描述符数（跳过 `.` / `..`）。
/// 对 self 计数时，opendir 自身的 dirfd 会短暂出现在列表中，但每次调用方式相同，
/// 适合做“启停前后是否增长”的相对比较，不适合当作绝对业务 fd 清单。
inline int count_proc_fds(pid_t pid) {
  const std::string path = "/proc/" + std::to_string(static_cast<long>(pid)) + "/fd";
  DIR* dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    return -1;
  }
  int n = 0;
  while (const dirent* ent = ::readdir(dir)) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    ++n;
  }
  ::closedir(dir);
  return n;
}

/// 读取 `/proc/<pid>/status` 的 `Threads:`；失败返回 -1。
inline int count_proc_threads(pid_t pid) {
  const std::string path =
      "/proc/" + std::to_string(static_cast<long>(pid)) + "/status";
  std::ifstream in(path);
  if (!in) {
    return -1;
  }
  std::string line;
  while (std::getline(in, line)) {
    constexpr std::string_view kPrefix = "Threads:";
    if (line.compare(0, kPrefix.size(), kPrefix) != 0) {
      continue;
    }
    try {
      return std::stoi(line.substr(kPrefix.size()));
    } catch (...) {
      return -1;
    }
  }
  return -1;
}

/// CTest 约定：进程返回 77 且测试设置了 SKIP_RETURN_CODE 时记为 Skipped。
inline constexpr int kSkipExitCode = 77;

struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Skip : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline int& failure_count() {
  static int n = 0;
  return n;
}

inline void expect(bool cond, const char* expr, const char* file, int line) {
  if (!cond) {
    ++failure_count();
    std::cerr << file << ":" << line << " EXPECT failed: " << expr << "\n";
  }
}

inline void require(bool cond, const char* expr, const char* file, int line) {
  if (!cond) {
    throw Failure(std::string(file) + ":" + std::to_string(line) +
                  " REQUIRE failed: " + expr);
  }
}

[[noreturn]] inline void skip(const std::string& reason) { throw Skip(reason); }

using TestFn = void (*)();

inline std::vector<std::pair<const char*, TestFn>>& registry() {
  static std::vector<std::pair<const char*, TestFn>> tests;
  return tests;
}

struct Registrar {
  Registrar(const char* name, TestFn fn) { registry().emplace_back(name, fn); }
};

inline int run_all() {
  int failed_cases = 0;
  int skipped_cases = 0;
  for (const auto& [name, fn] : registry()) {
    failure_count() = 0;
    std::cout << "[ RUN  ] " << name << "\n";
    try {
      fn();
      if (failure_count() != 0) {
        ++failed_cases;
        std::cout << "[ FAIL ] " << name << " (" << failure_count() << " asserts)\n";
      } else {
        std::cout << "[ PASS ] " << name << "\n";
      }
    } catch (const Skip& skip_ex) {
      ++skipped_cases;
      std::cout << "[ SKIP ] " << name << ": " << skip_ex.what() << "\n";
    } catch (const std::exception& ex) {
      ++failed_cases;
      std::cout << "[ FAIL ] " << name << " exception: " << ex.what() << "\n";
    }
  }
  const std::size_t passed =
      registry().size() - static_cast<std::size_t>(failed_cases) -
      static_cast<std::size_t>(skipped_cases);
  std::cout << "==== " << passed << " passed, " << failed_cases << " failed, "
            << skipped_cases << " skipped ====\n";
  if (failed_cases != 0) {
    return EXIT_FAILURE;
  }
  // 整进程都被跳过时返回 77，供可选集成测试的 CTest SKIP_RETURN_CODE 使用。
  if (skipped_cases != 0 && passed == 0) {
    return kSkipExitCode;
  }
  return EXIT_SUCCESS;
}

}  // namespace rcr::test

#define RCR_EXPECT(expr) \
  ::rcr::test::expect(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define RCR_REQUIRE(expr) \
  ::rcr::test::require(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define RCR_SKIP(reason) ::rcr::test::skip(reason)

#define RCR_TEST(name)                                                       \
  static void name();                                                        \
  static ::rcr::test::Registrar name##_registrar{#name, &name};              \
  static void name()

#define RCR_TEST_MAIN() \
  int main() { return ::rcr::test::run_all(); }
