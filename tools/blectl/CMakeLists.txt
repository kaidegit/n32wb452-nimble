#
# blectl — macOS command-line NimBLE host over the N32WB452 USB CDC bridge.
#
#   blectl ── /dev/cu.usbmodem* (H4) ── N32WB452 ── SPI ── NZ8801 controller
#
cmake_minimum_required(VERSION 3.20)
project(blectl C CXX)

set(NIMBLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/mynewt-nimble)

if(APPLE)
    # timer_t and friends in the linux NPL need POSIX2001 visibility
    add_compile_definitions(_DARWIN_C_SOURCE _DEFAULT_SOURCE)
    add_compile_options(-Wno-unused-parameter -Wno-sign-compare)
endif()

# ------------------------------------------------------------- NimBLE ----
add_library(nimble_host_pc STATIC)

file(GLOB NIMBLE_HOST_SRC    ${NIMBLE_ROOT}/nimble/host/src/*.c)
file(GLOB NIMBLE_SVC_SRC     ${NIMBLE_ROOT}/nimble/host/services/gap/src/*.c
                              ${NIMBLE_ROOT}/nimble/host/services/gatt/src/*.c)
file(GLOB NIMBLE_UTIL_SRC    ${NIMBLE_ROOT}/nimble/host/util/src/*.c)
file(GLOB NIMBLE_STORE_SRC   ${NIMBLE_ROOT}/nimble/host/store/config/src/*.c)
file(GLOB NIMBLE_TRANSP_SRC  ${NIMBLE_ROOT}/nimble/transport/src/*.c)
file(GLOB NIMBLE_PORTING_SRC ${NIMBLE_ROOT}/porting/nimble/src/*.c)
file(GLOB TINYCRYPT_SRC      ${NIMBLE_ROOT}/ext/tinycrypt/src/*.c)

list(FILTER NIMBLE_PORTING_SRC EXCLUDE REGEX ".*hal_timer\\.c|.*os_cputime.*\\.c")
list(FILTER NIMBLE_TRANSP_SRC EXCLUDE REGEX ".*monitor\\.c|.*rx_task\\.c")

if(NOT APPLE)
    target_sources(nimble_host_pc PRIVATE
        ${NIMBLE_ROOT}/porting/npl/linux/src/os_callout.c
        ${NIMBLE_ROOT}/porting/npl/linux/src/os_mutex.c
        ${NIMBLE_ROOT}/porting/npl/linux/src/os_sem.c
    )
endif()

target_sources(nimble_host_pc PRIVATE
    ${NIMBLE_HOST_SRC}
    ${NIMBLE_SVC_SRC}
    ${NIMBLE_UTIL_SRC}
    ${NIMBLE_STORE_SRC}
    ${NIMBLE_TRANSP_SRC}
    ${NIMBLE_PORTING_SRC}
    ${NIMBLE_ROOT}/porting/npl/linux/src/os_atomic.c
    ${NIMBLE_ROOT}/porting/npl/linux/src/os_eventq.cc
    ${NIMBLE_ROOT}/porting/npl/linux/src/os_task.c
    ${NIMBLE_ROOT}/porting/npl/linux/src/os_time.c
    ${TINYCRYPT_SRC}
)

if(APPLE)
    # macOS lacks POSIX timers, sem_timedwait() and pthread_mutex_timedlock();
    # these local backends implement the same NPL contracts.
    target_sources(nimble_host_pc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/npl_darwin/os_callout.c
        ${CMAKE_CURRENT_SOURCE_DIR}/npl_darwin/os_mutex.c
        ${CMAKE_CURRENT_SOURCE_DIR}/npl_darwin/os_sem.c
    )
endif()

target_include_directories(nimble_host_pc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc      # syscfg + log stubs first
    ${NIMBLE_ROOT}/nimble/include
    ${NIMBLE_ROOT}/nimble/host/include
    ${NIMBLE_ROOT}/nimble/host/util/include
    ${NIMBLE_ROOT}/nimble/host/services/gap/include
    ${NIMBLE_ROOT}/nimble/host/services/gatt/include
    ${NIMBLE_ROOT}/nimble/host/store/config/include
    ${NIMBLE_ROOT}/nimble/transport/include
    ${NIMBLE_ROOT}/ext/tinycrypt/include
    ${NIMBLE_ROOT}/porting/nimble/include
    ${NIMBLE_ROOT}/porting/npl/linux/include
)
target_compile_definitions(nimble_host_pc PUBLIC NIMBLE_CFG_CONTROLLER=0)
find_package(Threads REQUIRED)
target_link_libraries(nimble_host_pc PUBLIC Threads::Threads)

# --------------------------------------------------------------- tool -----
add_executable(blectl
    main.c
    hci_cdc.c
    bridge_hid.cpp
    boot_config.cpp
)
target_link_libraries(blectl PRIVATE nimble_host_pc)
if(APPLE)
    target_link_libraries(blectl PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../proto ${CMAKE_CURRENT_BINARY_DIR}/bridge_protocol)
find_path(HIDAPI_INCLUDE_DIR hidapi.h PATH_SUFFIXES hidapi REQUIRED)
find_library(HIDAPI_LIBRARY NAMES hidapi hidapi-hidraw hidapi-libusb REQUIRED)
target_include_directories(blectl PRIVATE ${HIDAPI_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)
target_compile_features(blectl PRIVATE cxx_std_17)
target_link_libraries(blectl PRIVATE bridge_protocol ${HIDAPI_LIBRARY})
configure_file(bridge_boot.json bridge_boot.json COPYONLY)
target_compile_definitions(blectl PRIVATE
    BLECTL_DEFAULT_CONFIG="${CMAKE_CURRENT_BINARY_DIR}/bridge_boot.json")

include(CTest)
if(BUILD_TESTING)
    add_executable(bridge_protocol_test tests/protocol_test.c)
    target_link_libraries(bridge_protocol_test PRIVATE bridge_protocol)
    target_compile_options(bridge_protocol_test PRIVATE -UNDEBUG)
    add_test(NAME bridge_protocol COMMAND bridge_protocol_test)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME boot_config COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/boot_config_test.py
        $<TARGET_FILE:blectl> ${CMAKE_CURRENT_SOURCE_DIR}/bridge_boot.json)

    add_executable(bridge_control_test tests/control_test.c)
    target_link_libraries(bridge_control_test PRIVATE bridge_protocol)
    target_include_directories(bridge_control_test PRIVATE
        tests/stubs ../../app ../../components/ble
        ../../third_party/cherryusb ../../third_party/cherryusb/core
        ../../third_party/cherryusb/common)
    target_compile_options(bridge_control_test PRIVATE -UNDEBUG)
    target_compile_definitions(bridge_control_test PRIVATE CONFIG_USB_MEMCPY_DISABLE)
    add_test(NAME bridge_control COMMAND bridge_control_test)
endif()
