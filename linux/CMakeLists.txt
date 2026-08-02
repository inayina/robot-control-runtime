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
option(RCR_ENABLE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(RCR_ENABLE_TSAN "Build with ThreadSanitizer (exclusive with ASan)" OFF)

if(RCR_ENABLE_ASAN AND RCR_ENABLE_TSAN)
  message(FATAL_ERROR "RCR_ENABLE_ASAN and RCR_ENABLE_TSAN are mutually exclusive")
endif()

add_library(rcr
  src/core/mailbox.cpp
  src/core/runtime.cpp
  src/core/runtime_events.cpp
  src/core/scheduler.cpp
  src/core/state_machine.cpp
  src/core/stats.cpp
  src/core/time.cpp
  src/core/trace.cpp
  src/core/watchdog.cpp
  src/can/can_v1.cpp
  src/linux/can_io_loop.cpp
  src/linux/epoll_reactor.cpp
  src/linux/owned_fd.cpp
  src/linux/socketcan.cpp
  src/linux/vcan.cpp
  src/daemon/runtime_daemon.cpp
  src/sim/node_sim.cpp
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

function(rcr_apply_sanitizers target)
  if(RCR_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  elseif(RCR_ENABLE_TSAN)
    target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=thread)
  endif()
endfunction()

rcr_apply_sanitizers(rcr)

add_executable(rcr_benchmark apps/rcr_benchmark.cpp)
target_link_libraries(rcr_benchmark PRIVATE rcr)
rcr_apply_sanitizers(rcr_benchmark)

add_executable(rcrd apps/rcrd.cpp)
target_link_libraries(rcrd PRIVATE rcr)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcrd PRIVATE -Wall -Wextra -Wpedantic)
endif()
rcr_apply_sanitizers(rcrd)

add_executable(rcr_node_sim apps/rcr_node_sim.cpp)
target_link_libraries(rcr_node_sim PRIVATE rcr)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_node_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()
rcr_apply_sanitizers(rcr_node_sim)

add_executable(rcr_vcan_acceptance apps/rcr_vcan_acceptance.cpp)
target_link_libraries(rcr_vcan_acceptance PRIVATE rcr)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_vcan_acceptance PRIVATE -Wall -Wextra -Wpedantic)
endif()
rcr_apply_sanitizers(rcr_vcan_acceptance)
add_dependencies(rcr_vcan_acceptance rcr_node_sim)

add_executable(rcr_fault_matrix apps/rcr_fault_matrix.cpp)
target_link_libraries(rcr_fault_matrix PRIVATE rcr)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_fault_matrix PRIVATE -Wall -Wextra -Wpedantic)
endif()
rcr_apply_sanitizers(rcr_fault_matrix)
add_dependencies(rcr_fault_matrix rcr_node_sim rcrd)

include(GNUInstallDirs)
install(TARGETS rcr
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS rcr_benchmark rcrd rcr_node_sim rcr_vcan_acceptance rcr_fault_matrix
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/rcr DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(RCR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
