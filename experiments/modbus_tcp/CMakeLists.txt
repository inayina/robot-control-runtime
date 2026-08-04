cmake_minimum_required(VERSION 3.20)
project(rcr_modbus_tcp_experiment
  VERSION 0.1.0
  DESCRIPTION "Hand-rolled Modbus TCP learning experiment (not Runtime V1)"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RCR_MBUS_BUILD_TESTS "Build Modbus TCP experiment tests" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMODBUS REQUIRED libmodbus)

add_library(rcr_mbus
  src/codec.cpp
  src/framing.cpp
  src/register_map.cpp
  src/client.cpp
  src/server.cpp
)
add_library(rcr::mbus ALIAS rcr_mbus)

target_include_directories(rcr_mbus
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(rcr_mbus PUBLIC cxx_std_20)
target_link_libraries(rcr_mbus PUBLIC pthread)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_mbus PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

add_executable(mbus_ref_server apps/mbus_ref_server.cpp)
target_link_libraries(mbus_ref_server PRIVATE rcr_mbus)

add_executable(mbus_demo_client apps/mbus_demo_client.cpp)
target_link_libraries(mbus_demo_client PRIVATE rcr_mbus)

if(RCR_MBUS_BUILD_TESTS)
  enable_testing()

  function(rcr_mbus_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE rcr_mbus)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  rcr_mbus_test(test_codec)
  rcr_mbus_test(test_framing)
  rcr_mbus_test(test_localhost)
  rcr_mbus_test(test_faults)

  add_executable(test_libmodbus_interop tests/test_libmodbus_interop.cpp)
  target_include_directories(test_libmodbus_interop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${LIBMODBUS_INCLUDE_DIRS}
  )
  target_link_libraries(test_libmodbus_interop PRIVATE rcr_mbus ${LIBMODBUS_LIBRARIES})
  target_compile_options(test_libmodbus_interop PRIVATE ${LIBMODBUS_CFLAGS_OTHER})
  add_test(NAME test_libmodbus_interop COMMAND test_libmodbus_interop)
endif()

message(STATUS "libmodbus ${LIBMODBUS_VERSION} enabled for interop tests")
