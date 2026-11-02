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
)
target_link_libraries(blectl PRIVATE nimble_host_pc)
