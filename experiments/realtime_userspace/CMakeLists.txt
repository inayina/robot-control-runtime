cmake_minimum_required(VERSION 3.20)
project(rcr_realtime_userspace_rt3
  VERSION 0.1.0
  DESCRIPTION "RT3 userspace realtime programming fixtures (not Runtime Core)"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(rcr_rt3_exe name)
  add_executable(${name} apps/${name}.cpp)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_link_libraries(${name} PRIVATE pthread)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endfunction()

rcr_rt3_exe(rcr_rt3_mlock)
rcr_rt3_exe(rcr_rt3_pi_mutex)
rcr_rt3_exe(rcr_rt3_cycle_path)

enable_testing()
add_test(NAME rt3_mlock_smoke COMMAND rcr_rt3_mlock --bytes 1048576 --self-check)
add_test(NAME rt3_pi_smoke COMMAND rcr_rt3_pi_mutex --work-ms 20 --self-check)
set_tests_properties(rt3_pi_smoke PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME rt3_cycle_smoke COMMAND rcr_rt3_cycle_path --mode compare --ticks 500 --self-check)
