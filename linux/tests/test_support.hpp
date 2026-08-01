#pragma once

// Linux Runtime 使用的零依赖测试辅助，不作为固件测试框架共享。

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace rcr::test {

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
