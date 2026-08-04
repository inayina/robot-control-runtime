cmake_minimum_required(VERSION 3.20)
project(rcr_multibus_observer
  VERSION 0.1.0
  DESCRIPTION "Typed CAN and Modbus observation experiment"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RCR_MULTIBUS_BUILD_TESTS "Build multibus observer tests" ON)

# 这是实验目录自己的组合构建入口，不改变 linux/ 与 modbus_tcp/ 各自可独立构建的边界。
# EXCLUDE_FROM_ALL 只拉取本实验实际链接的库，不把两个子工程的应用/测试变成默认目标。
set(RCR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RCR_MBUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../linux
                 ${CMAKE_CURRENT_BINARY_DIR}/rcr_linux EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../modbus_tcp
                 ${CMAKE_CURRENT_BINARY_DIR}/rcr_modbus EXCLUDE_FROM_ALL)

add_library(rcr_multibus
  src/can_status_adapter.cpp
  src/modbus_temperature_adapter.cpp
  src/observation.cpp
)
add_library(rcr::multibus ALIAS rcr_multibus)

target_include_directories(rcr_multibus
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(rcr_multibus PUBLIC cxx_std_20)
target_link_libraries(rcr_multibus PUBLIC rcr::rcr)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_multibus PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

add_executable(rcr_multibus_observer apps/rcr_multibus_observer.cpp)
target_link_libraries(rcr_multibus_observer PRIVATE rcr_multibus rcr::mbus)

add_executable(mbus_sensor_server apps/mbus_sensor_server.cpp)
target_link_libraries(mbus_sensor_server PRIVATE rcr::mbus)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_multibus_observer PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(mbus_sensor_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RCR_MULTIBUS_BUILD_TESTS)
  enable_testing()
  add_executable(test_observation tests/test_observation.cpp)
  target_link_libraries(test_observation PRIVATE rcr_multibus)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_observation PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME test_observation COMMAND test_observation)
endif()
