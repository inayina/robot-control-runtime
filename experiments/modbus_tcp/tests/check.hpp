#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

namespace rcr::mbus::test {

inline void check(bool cond, const std::string& msg, const char* file, int line) {
  if (!cond) {
    std::cerr << "CHECK failed at " << file << ":" << line << " — " << msg << "\n";
    std::exit(1);
  }
}

}  // namespace rcr::mbus::test

#define CHECK(cond) \
  ::rcr::mbus::test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) \
  ::rcr::mbus::test::check(static_cast<bool>(cond), (msg), __FILE__, __LINE__)
