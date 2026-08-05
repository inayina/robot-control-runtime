cmake_minimum_required(VERSION 3.20)
project(rcr_realtime_segmented_rt6
  VERSION 0.1.0
  DESCRIPTION "RT6 segmented latency fixture (software peer; not Runtime Core)"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 复用 PeriodicScheduler / EventFd；EXCLUDE_FROM_ALL 不把 linux 应用变成默认目标。
set(RCR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../linux
                 ${CMAKE_CURRENT_BINARY_DIR}/rcr_linux EXCLUDE_FROM_ALL)

add_executable(rcr_rt6_segments apps/rcr_rt6_segments.cpp)
target_link_libraries(rcr_rt6_segments PRIVATE rcr::rcr)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_rt6_segments PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

enable_testing()
add_test(NAME rt6_segments_smoke
  COMMAND rcr_rt6_segments --mode compare --ticks 400 --period-us 1000
          --busy-us 400 --self-check)
set_tests_properties(rt6_segments_smoke PROPERTIES SKIP_RETURN_CODE 77)
