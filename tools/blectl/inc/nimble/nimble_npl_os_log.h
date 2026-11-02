/* Apply the desktop log floor to the actual NPL backend. The upstream example
 * uses vprintf unconditionally and bypasses the Mynewt log shim. */
#ifndef BLECTL_NIMBLE_NPL_OS_LOG_H
#define BLECTL_NIMBLE_NPL_OS_LOG_H

#include <stdarg.h>
#include "log_shim.h"

#define BLE_NPL_LOG_IMPL(level) \
    static inline void _BLE_NPL_LOG_CAT(BLE_NPL_LOG_MODULE, \
            _BLE_NPL_LOG_CAT(_, level))(const char *format, ...) \
    { \
        if (LOG_LEVEL_##level < BLECTL_NIMBLE_LOG_LEVEL) return; \
        va_list args; \
        va_start(args, format); \
        vfprintf(stderr, format, args); \
        va_end(args); \
    }

#endif
