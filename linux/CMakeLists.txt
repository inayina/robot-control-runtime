cmake_minimum_required(VERSION 3.20)
project(robot_control_runtime
  VERSION 0.1.0
  DESCRIPTION "Orange Pi Linux edge runtime for scheduling, supervision and SocketCAN"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RCR_BUILD_TESTS "Build unit tests" ON)

add_library(rcr
  src/can_v1.cpp
  src/epoll_reactor.cpp
  src/mailbox.cpp
  src/runtime.cpp
  src/scheduler.cpp
  src/state_machine.cpp
  src/socketcan.cpp
  src/time.cpp
  src/trace.cpp
  src/vcan.cpp
  src/watchdog.cpp
)
add_library(rcr::rcr ALIAS rcr)

target_include_directories(rcr
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(rcr PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

# 周期线程与 SocketCAN 实现依赖 Linux 线程库；核心不引入 ROS2 或 MCU SDK。
target_link_libraries(rcr PUBLIC pthread)

add_executable(rcr_benchmark apps/rcr_benchmark.cpp)
target_link_libraries(rcr_benchmark PRIVATE rcr)

include(GNUInstallDirs)
install(TARGETS rcr
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/rcr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(RCR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
