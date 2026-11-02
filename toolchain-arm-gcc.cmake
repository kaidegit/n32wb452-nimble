# ARM GCC toolchain for N32WB452 (Cortex-M4F).
#
# Toolchain resolution order:
#   1. ARM_GCC_PATH / GNU_ARM_PATH environment variable
#   2. arm-none-eabi-gcc found in PATH
#   3. Known local install locations on this machine
if(DEFINED TOOLCHAIN_FILE_ALREADY_PROCESSED)
    return()
endif()
set(TOOLCHAIN_FILE_ALREADY_PROCESSED TRUE)

set(_ARM_GCC_CANDIDATES
    "$ENV{ARM_GCC_PATH}"
    "$ENV{GNU_ARM_PATH}"
    "/Users/kai/DevTools/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi"
    "/opt/homebrew/opt/gcc-arm-embedded"
    "/usr/local/gcc-arm-embedded")

set(TOOLCHAIN_PATH "")
foreach(_c IN LISTS _ARM_GCC_CANDIDATES)
    if(_c AND EXISTS "${_c}/bin/arm-none-eabi-gcc")
        set(TOOLCHAIN_PATH "${_c}")
        break()
    endif()
endforeach()

if(NOT TOOLCHAIN_PATH)
    find_program(ARM_GCC_COMPILER arm-none-eabi-gcc)
    if(ARM_GCC_COMPILER)
        get_filename_component(_bin ${ARM_GCC_COMPILER} DIRECTORY)
        get_filename_component(TOOLCHAIN_PATH ${_bin} DIRECTORY)
    endif()
endif()

if(NOT TOOLCHAIN_PATH)
    message(FATAL_ERROR
        "arm-none-eabi-gcc not found. Set ARM_GCC_PATH or add it to PATH.")
endif()
message(STATUS "ARM GCC toolchain: ${TOOLCHAIN_PATH}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PATH}/bin/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PATH}/bin/arm-none-eabi-g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PATH}/bin/arm-none-eabi-gcc")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PATH}/bin/arm-none-eabi-objcopy" CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PATH}/bin/arm-none-eabi-objdump" CACHE FILEPATH "")
set(CMAKE_SIZE         "${TOOLCHAIN_PATH}/bin/arm-none-eabi-size"    CACHE FILEPATH "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M4F with hard float, tuned like the vendor SDK builds.
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
set(CMAKE_C_FLAGS_INIT
    "${CPU_FLAGS} -std=gnu11 -O2 -g3 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${CPU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage --specs=nano.specs --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
