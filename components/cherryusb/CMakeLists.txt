# CherryUSB device stack (CDC ACM only, fsdev port, no-OS bare mode).
add_library(cherryusb STATIC
    ${CHERRYUSB_ROOT}/core/usbd_core.c
    ${CHERRYUSB_ROOT}/class/cdc/usbd_cdc_acm.c
    ${CHERRYUSB_ROOT}/port/fsdev/usb_dc_fsdev.c
)
target_include_directories(cherryusb PUBLIC
    ${CHERRYUSB_ROOT}                 # usb_config.h
    ${CHERRYUSB_ROOT}/common
    ${CHERRYUSB_ROOT}/core
    ${CHERRYUSB_ROOT}/class/cdc
    ${CHERRYUSB_ROOT}/port/fsdev
)
# Default (non-EP0_THREAD) build runs entirely from USB IRQ context; the
# bridge only arms endpoint transfers and uses FreeRTOS FromISR primitives
# in its own callbacks (USB IRQ priority is set to 6 in the glue file).
target_link_libraries(cherryusb PUBLIC n32wb452_sdk)
