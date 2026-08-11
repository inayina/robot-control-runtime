# Linux Runtime 最小断言测试框架：零第三方依赖，每个测试可执行文件相互独立。
add_library(rcr_test_support INTERFACE)
target_include_directories(rcr_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

function(rcr_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE rcr rcr_test_support ${ARGN})
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  # sanitizer 必须同时作用于静态库与可执行文件，否则 TSan/ASan 会在链接期缺符号。
  if(RCR_ENABLE_ASAN)
    target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
  elseif(RCR_ENABLE_TSAN)
    target_compile_options(${name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=thread)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

rcr_add_test(test_mailbox)
rcr_add_test(test_state_machine)
rcr_add_test(test_fake_can)
rcr_add_test(test_vcan)
rcr_add_test(test_socketcan)
rcr_add_test(test_can_v1)
rcr_add_test(test_node_sim)
rcr_add_test(test_scheduler)
rcr_add_test(test_epoll_reactor)
rcr_add_test(test_watchdog)
rcr_add_test(test_trace)
rcr_add_test(test_runtime)
rcr_add_test(test_owned_fd)
rcr_add_test(test_runtime_events)
rcr_add_test(test_stats)
rcr_add_test(test_workbench_runner rcr::workbench)
rcr_add_test(test_workbench_runtime_adapter rcr::workbench)
rcr_add_test(test_workbench_can_health rcr::workbench)
rcr_add_test(test_workbench_result_writer rcr::workbench)
rcr_add_test(test_mock_actuator_profile rcr::workbench)

# 可选 vcan 回环：接口缺失时进程返回 77，CTest 记为 Skipped，避免假 PASS。
# 阶段验收（缺失即失败）不加入默认 CTest，手动运行：
#   ./build/linux/tests/test_socketcan_vcan --require-vcan
add_executable(test_socketcan_vcan test_socketcan_vcan.cpp)
target_link_libraries(test_socketcan_vcan PRIVATE rcr rcr_test_support)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_socketcan_vcan PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(RCR_ENABLE_ASAN)
  target_compile_options(test_socketcan_vcan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_socketcan_vcan PRIVATE -fsanitize=address,undefined)
elseif(RCR_ENABLE_TSAN)
  target_compile_options(test_socketcan_vcan PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(test_socketcan_vcan PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_socketcan_vcan COMMAND test_socketcan_vcan)
set_tests_properties(test_socketcan_vcan PROPERTIES SKIP_RETURN_CODE 77)

# RuntimeDaemon / rcrd 进程验收：缺 vcan0 时部分用例 Skip。
add_executable(test_runtime_daemon test_runtime_daemon.cpp)
target_link_libraries(test_runtime_daemon PRIVATE rcr rcr_test_support)
target_compile_definitions(test_runtime_daemon PRIVATE
  RCR_NODE_SIM_PATH="$<TARGET_FILE:rcr_node_sim>")
add_dependencies(test_runtime_daemon rcr_node_sim)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_runtime_daemon PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(RCR_ENABLE_ASAN)
  target_compile_options(test_runtime_daemon PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_runtime_daemon PRIVATE -fsanitize=address,undefined)
elseif(RCR_ENABLE_TSAN)
  target_compile_options(test_runtime_daemon PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(test_runtime_daemon PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_runtime_daemon COMMAND test_runtime_daemon)
set_tests_properties(test_runtime_daemon PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_rcrd_process test_rcrd_process.cpp)
target_link_libraries(test_rcrd_process PRIVATE rcr rcr_test_support)
target_compile_definitions(test_rcrd_process PRIVATE
  RCR_RCRD_PATH="$<TARGET_FILE:rcrd>"
  RCR_NODE_SIM_PATH="$<TARGET_FILE:rcr_node_sim>")
add_dependencies(test_rcrd_process rcrd rcr_node_sim)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_rcrd_process PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(RCR_ENABLE_ASAN)
  target_compile_options(test_rcrd_process PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_rcrd_process PRIVATE -fsanitize=address,undefined)
elseif(RCR_ENABLE_TSAN)
  target_compile_options(test_rcrd_process PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(test_rcrd_process PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_rcrd_process COMMAND test_rcrd_process)
set_tests_properties(test_rcrd_process PROPERTIES SKIP_RETURN_CODE 77)

# Workbench 的真实 Runtime/vcan 纵向切片；缺 vcan0 时必须显式 Skip。
add_executable(test_workbench_can_health_vcan test_workbench_can_health_vcan.cpp)
target_link_libraries(test_workbench_can_health_vcan PRIVATE
  rcr rcr::workbench rcr_test_support)
target_compile_definitions(test_workbench_can_health_vcan PRIVATE
  RCR_NODE_SIM_PATH="$<TARGET_FILE:rcr_node_sim>")
add_dependencies(test_workbench_can_health_vcan rcr_node_sim)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_workbench_can_health_vcan PRIVATE
    -Wall -Wextra -Wpedantic)
endif()
if(RCR_ENABLE_ASAN)
  target_compile_options(test_workbench_can_health_vcan PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_workbench_can_health_vcan PRIVATE
    -fsanitize=address,undefined)
elseif(RCR_ENABLE_TSAN)
  target_compile_options(test_workbench_can_health_vcan PRIVATE
    -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(test_workbench_can_health_vcan PRIVATE -fsanitize=thread)
endif()
add_test(NAME test_workbench_can_health_vcan
  COMMAND test_workbench_can_health_vcan)
set_tests_properties(test_workbench_can_health_vcan PROPERTIES
  SKIP_RETURN_CODE 77)
