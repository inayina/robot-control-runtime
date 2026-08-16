cmake_minimum_required(VERSION 3.20)

project(rcr_stm32f103_can_node LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(rcr_mcu_logic STATIC
  src/can_v1.c
  src/node.c
  src/servo_pwm.c
  src/input_debounce.c
)
target_include_directories(rcr_mcu_logic PUBLIC include)
target_compile_options(rcr_mcu_logic PRIVATE
  -Wall
  -Wextra
  -Wconversion
  -Wshadow
  -Werror
)

if(CMAKE_CROSSCOMPILING)
  add_executable(rcr_stm32f103_can_node
    src/main.c
    src/platform_stm32f103.c
    src/startup_stm32f103c8.S
  )
  set_target_properties(rcr_stm32f103_can_node PROPERTIES SUFFIX ".elf")
  target_link_libraries(rcr_stm32f103_can_node PRIVATE rcr_mcu_logic gcc)
  target_link_options(rcr_stm32f103_can_node PRIVATE
    -nostdlib
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/rcr_stm32f103_can_node.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32f103c8.ld
  )

  add_custom_command(TARGET rcr_stm32f103_can_node POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:rcr_stm32f103_can_node>
            ${CMAKE_CURRENT_BINARY_DIR}/rcr_stm32f103_can_node.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:rcr_stm32f103_can_node>
            ${CMAKE_CURRENT_BINARY_DIR}/rcr_stm32f103_can_node.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:rcr_stm32f103_can_node>
    VERBATIM
  )

  add_executable(rcr_stm32f103_can_arbitration_probe
    src/arbitration_probe.c
    src/platform_stm32f103.c
    src/startup_stm32f103c8.S
  )
  set_target_properties(rcr_stm32f103_can_arbitration_probe
    PROPERTIES SUFFIX ".elf")
  target_link_libraries(rcr_stm32f103_can_arbitration_probe
    PRIVATE rcr_mcu_logic gcc)
  target_link_options(rcr_stm32f103_can_arbitration_probe PRIVATE
    -nostdlib
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/rcr_stm32f103_can_arbitration_probe.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32f103c8.ld
  )
  add_custom_command(TARGET rcr_stm32f103_can_arbitration_probe POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:rcr_stm32f103_can_arbitration_probe>
            ${CMAKE_CURRENT_BINARY_DIR}/rcr_stm32f103_can_arbitration_probe.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:rcr_stm32f103_can_arbitration_probe>
    VERBATIM
  )
else()
  enable_testing()
  add_executable(rcr_stm32f103_logic_tests tests/test_logic.c)
  target_link_libraries(rcr_stm32f103_logic_tests PRIVATE rcr_mcu_logic)
  target_compile_options(rcr_stm32f103_logic_tests PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Werror
  )
  add_test(NAME rcr_stm32f103_logic_tests COMMAND rcr_stm32f103_logic_tests)
endif()
