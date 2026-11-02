# Apache NimBLE host stack (host-only build, FreeRTOS NPL backend,
# custom SPI HCI transport implemented in components/ble/hci_spi.c).
add_library(nimble_host STATIC)

file(GLOB NIMBLE_HOST_SRC     ${NIMBLE_ROOT}/nimble/host/src/*.c)
file(GLOB NIMBLE_SVC_SRC      ${NIMBLE_ROOT}/nimble/host/services/gap/src/*.c
                               ${NIMBLE_ROOT}/nimble/host/services/gatt/src/*.c)
file(GLOB NIMBLE_UTIL_SRC     ${NIMBLE_ROOT}/nimble/host/util/src/*.c)
file(GLOB NIMBLE_STORE_SRC    ${NIMBLE_ROOT}/nimble/host/store/config/src/*.c)
file(GLOB NIMBLE_TRANSP_SRC   ${NIMBLE_ROOT}/nimble/transport/src/*.c)
file(GLOB NIMBLE_PORTING_SRC  ${NIMBLE_ROOT}/porting/nimble/src/*.c)
file(GLOB TINYCRYPT_SRC       ${NIMBLE_ROOT}/ext/tinycrypt/src/*.c)

# Porting layer: drop the Nordic hal_timer (controller-only) and the
# cpu-time helpers (controller-only).
list(FILTER NIMBLE_PORTING_SRC EXCLUDE REGEX ".*hal_timer\\.c|.*os_cputime.*\\.c")
# Transport: keep only the generic framework; HCI monitor and the
# rx-task-based LL transports are not used by our custom transport.
list(FILTER NIMBLE_TRANSP_SRC EXCLUDE REGEX ".*monitor\\.c|.*rx_task\\.c")

target_sources(nimble_host PRIVATE
    ${NIMBLE_HOST_SRC}
    ${NIMBLE_SVC_SRC}
    ${NIMBLE_UTIL_SRC}
    ${NIMBLE_STORE_SRC}
    ${NIMBLE_TRANSP_SRC}
    ${NIMBLE_PORTING_SRC}
    ${NIMBLE_ROOT}/porting/npl/freertos/src/npl_os_freertos.c
    ${NIMBLE_ROOT}/porting/npl/freertos/src/nimble_port_freertos.c
    ${TINYCRYPT_SRC}
)

target_include_directories(nimble_host PUBLIC
    # our overrides come first: syscfg.h + log stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${NIMBLE_ROOT}/nimble/include
    ${NIMBLE_ROOT}/nimble/host/include
    ${NIMBLE_ROOT}/nimble/host/util/include
    ${NIMBLE_ROOT}/nimble/host/services/gap/include
    ${NIMBLE_ROOT}/nimble/host/services/gatt/include
    ${NIMBLE_ROOT}/nimble/host/store/config/include
    ${NIMBLE_ROOT}/nimble/transport/include
    ${NIMBLE_ROOT}/ext/tinycrypt/include
    ${NIMBLE_ROOT}/porting/nimble/include
    ${NIMBLE_ROOT}/porting/npl/freertos/include
)

target_compile_definitions(nimble_host PUBLIC
    NIMBLE_CFG_CONTROLLER=0
    # provides SCB definitions needed by npl_os_freertos.c in_isr()
    NIMBLE_NPL_OS_EXTRA_INCLUDE="n32wb452.h"
)
# SDK headers for NIMBLE_NPL_OS_EXTRA_INCLUDE="n32wb452.h" (SCB access):
target_link_libraries(nimble_host PUBLIC n32wb452_sdk)

target_link_libraries(nimble_host PUBLIC freertos segger_rtt)
