#
# N32WB452 BLE firmware: Apache NimBLE host + closed-source NZ8801 controller.
#
# Architecture:
#   [nimble mode]  NimBLE host ── SPI HCI (H4) ── NZ8801 controller core
#   [bridge mode]  USB CDC ACM ←── H4 ──┘  (HCI passthrough for host-on-PC)
#
# One project, two firmwares. The mode is selected at configure time and
# injected as a compile definition:
#   cmake -B build-nimble  -DN32WB452_APP_MODE=nimble    (default)
#   cmake -B build-bridge  -DN32WB452_APP_MODE=bridge
#
cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain-arm-gcc.cmake")
endif()

project(n32wb452_ble C ASM)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ------------------------------------------------------------------ mode ---
set(N32WB452_APP_MODE "nimble" CACHE STRING "Firmware variant: nimble or bridge")
set_property(CACHE N32WB452_APP_MODE PROPERTY STRINGS nimble bridge)
if(NOT N32WB452_APP_MODE MATCHES "^(nimble|bridge)$")
    message(FATAL_ERROR "N32WB452_APP_MODE must be 'nimble' or 'bridge'")
endif()
message(STATUS "Firmware mode: ${N32WB452_APP_MODE}")

# ----------------------------------------------------------------- paths ---
set(SDK_ROOT       ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdk)
set(NIMBLE_ROOT    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mynewt-nimble)
set(FREERTOS_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/freertos)
set(RTT_ROOT       ${CMAKE_CURRENT_SOURCE_DIR}/third_party/segger_rtt)
set(CHERRYUSB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cherryusb)
set(CHIP_YAML      ${CMAKE_CURRENT_SOURCE_DIR}/N32WB452_Series.yaml)

# ============================================================ libraries ====

# ---------------------------------------------------------- Nations SDK ---
add_library(n32wb452_sdk STATIC)
file(GLOB SDK_PERIPH_SRC ${SDK_ROOT}/n32wb452_std_periph_driver/src/*.c)
target_sources(n32wb452_sdk PRIVATE
    ${SDK_PERIPH_SRC}
    ${SDK_ROOT}/CMSIS/device/system_n32wb452.c
    ${SDK_ROOT}/CMSIS/device/startup/startup_n32wb452_gcc.s
)
target_include_directories(n32wb452_sdk PUBLIC
    ${SDK_ROOT}/CMSIS/core
    ${SDK_ROOT}/CMSIS/device
    ${SDK_ROOT}/n32wb452_std_periph_driver/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/app        # n32wb452_conf.h + n32wb452.h shim
)
target_compile_definitions(n32wb452_sdk PUBLIC
    USE_STDPERIPH_DRIVER N32WB452 HSE_VALUE=16000000
)

# ------------------------------------------------------------- FreeRTOS ---
add_library(freertos STATIC)
target_sources(freertos PRIVATE
    ${FREERTOS_ROOT}/croutine.c
    ${FREERTOS_ROOT}/event_groups.c
    ${FREERTOS_ROOT}/list.c
    ${FREERTOS_ROOT}/queue.c
    ${FREERTOS_ROOT}/stream_buffer.c
    ${FREERTOS_ROOT}/tasks.c
    ${FREERTOS_ROOT}/timers.c
    ${FREERTOS_ROOT}/portable/GCC/ARM_CM4F/port.c
    ${FREERTOS_ROOT}/portable/MemMang/heap_4.c
)
target_include_directories(freertos PUBLIC
    ${FREERTOS_ROOT}/include
    ${FREERTOS_ROOT}/portable/GCC/ARM_CM4F
    ${CMAKE_CURRENT_SOURCE_DIR}/app        # FreeRTOSConfig.h
)
# Needed by heap_4 / port in some configs
target_compile_definitions(freertos PUBLIC __UVISION_VERSION=0)

# -------------------------------------------------------------- SEGGER ----
add_library(segger_rtt STATIC
    ${RTT_ROOT}/SEGGER_RTT.c
    ${RTT_ROOT}/SEGGER_RTT_printf.c
)
target_include_directories(segger_rtt PUBLIC ${RTT_ROOT})

# ------------------------------------------------- BLE hw platform layer ---
# SDK platform sources + closed-source driver archive + our glue.
add_library(ble_platform STATIC)
target_sources(ble_platform PRIVATE
    ${SDK_ROOT}/n32wb452_ble_driver/platform/Interface.c
    ${SDK_ROOT}/n32wb452_ble_driver/platform/Eif_timer.c
    ${SDK_ROOT}/n32wb452_ble_driver/platform/Eif_flash.c
    ${SDK_ROOT}/n32wb452_ble_driver/platform/Eif_debug.c
    components/ble/ble_platform.c
    components/ble/hci_fixup.c
)
target_include_directories(ble_platform PUBLIC
    components/ble
    components/ble/inc
    ${SDK_ROOT}/n32wb452_ble_driver
    ${SDK_ROOT}/n32wb452_ble_driver/inc
    ${SDK_ROOT}/n32wb452_ble_driver/platform/inc
    ${SDK_ROOT}/n32wb452_ble_driver/profile_inc
)
# N32WB452_BT_API: selects the maintained branch of the SDK Interface.c
# (the #else branch references undeclared locals and does not compile).
target_compile_definitions(ble_platform PUBLIC N32WB452_BT_API=1)
target_link_libraries(ble_platform PUBLIC n32wb452_sdk segger_rtt freertos)
# The IAR-built archive links fine with GNU arm-none-eabi (same ELF ABI);
# the vendor objects carry no IAR runtime dependencies we do not provide.
target_link_libraries(ble_platform INTERFACE
    ${SDK_ROOT}/n32wb452_ble_driver/lib/IAR/libn32wb452_ble.a
)

# ------------------------------------------------------- NimBLE host ------
add_subdirectory(components/nimble_host)

# ---------------------------------------------------------- CherryUSB -----
add_subdirectory(components/cherryusb)
add_subdirectory(proto)

# ============================================================== target ====
add_executable(n32wb452_ble.elf
    app/main.c
    app/app_nimble.c
    app/app_bridge.c
    app/bridge_control.c
    app/n32wb452_it.c
    app/syscalls.c
    components/ble/hci_spi.c
    components/ble/ble_spi_tx.c
)
target_include_directories(n32wb452_ble.elf PRIVATE app components/ble)
target_link_options(n32wb452_ble.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/app/n32wb452_flash.ld
    -Wl,--no-warn-rwx-segments
    -Wl,-Map=${CMAKE_BINARY_DIR}/n32wb452_ble.map
)
set_target_properties(n32wb452_ble.elf PROPERTIES
    SUFFIX ""
    OUTPUT_NAME "n32wb452_ble_${N32WB452_APP_MODE}"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app/n32wb452_flash.ld"
)

if(N32WB452_APP_MODE STREQUAL "nimble")
    target_compile_definitions(n32wb452_ble.elf PRIVATE APP_MODE_NIMBLE=1)
    target_sources(n32wb452_ble.elf PRIVATE
        components/gatt_perf/gatt_perf.c
        components/gatt_perf/perf_clock_n32.c)
    target_include_directories(n32wb452_ble.elf PRIVATE components/gatt_perf)
    target_link_libraries(n32wb452_ble.elf PRIVATE
        ble_platform nimble_host n32wb452_sdk segger_rtt)
else()
    target_compile_definitions(n32wb452_ble.elf PRIVATE APP_MODE_BRIDGE=1)
    # Link the platform hooks directly: weak fsdev hooks / vector aliases
    # otherwise satisfy all references without extracting the glue archive member.
    target_sources(n32wb452_ble.elf PRIVATE
        ${CHERRYUSB_ROOT}/port/fsdev/usb_glue_nations.c)
    target_link_libraries(n32wb452_ble.elf PRIVATE
        ble_platform cherryusb bridge_protocol n32wb452_sdk segger_rtt)
endif()

add_custom_command(TARGET n32wb452_ble.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:n32wb452_ble.elf>
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:n32wb452_ble.elf>
            $<TARGET_FILE:n32wb452_ble.elf>.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:n32wb452_ble.elf>
            $<TARGET_FILE:n32wb452_ble.elf>.hex
    COMMENT "Size / bin / hex"
)

# ------------------------------------------------------------- flashing ---
add_custom_target(flash
    COMMAND probe-rs download --chip-description-path ${CHIP_YAML}
            --chip N32WB452CEQ6 --speed 1000 $<TARGET_FILE:n32wb452_ble.elf>
    COMMAND probe-rs reset --chip-description-path ${CHIP_YAML}
            --chip N32WB452CEQ6
    DEPENDS n32wb452_ble.elf
    COMMENT "probe-rs: download + reset"
)
add_custom_target(rtt
    COMMAND probe-rs attach --chip-description-path ${CHIP_YAML}
            --chip N32WB452CEQ6 --rtt-channel-mode no-block-skip
            $<TARGET_FILE:n32wb452_ble.elf>
    DEPENDS n32wb452_ble.elf
    COMMENT "probe-rs: attach RTT console"
    USES_TERMINAL
)
