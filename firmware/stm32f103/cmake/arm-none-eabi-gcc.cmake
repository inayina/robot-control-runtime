set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "")

set(RCR_MCU_FLAGS
  "-mcpu=cortex-m3 -mthumb -ffreestanding -fdata-sections -ffunction-sections -fno-common -fno-unwind-tables -fno-asynchronous-unwind-tables"
)
set(CMAKE_C_FLAGS_INIT "${RCR_MCU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m3 -mthumb")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mcpu=cortex-m3 -mthumb")

