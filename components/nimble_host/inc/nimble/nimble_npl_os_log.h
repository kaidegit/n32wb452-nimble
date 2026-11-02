/* Override the upstream example logger, which prints every DEBUG/INFO packet
 * regardless of syscfg. Keep warnings/errors without formatting packet dumps. */
#ifndef N32_NIMBLE_NPL_OS_LOG_H
#define N32_NIMBLE_NPL_OS_LOG_H

#include <stdarg.h>
#include "log_shim.h"

#define BLE_NPL_LOG_IMPL(level) \
    static inline void _BLE_NPL_LOG_CAT(BLE_NPL_LOG_MODULE, \
            _BLE_NPL_LOG_CAT(_, level))(const char *format, ...) \
    { \
        if (LOG_LEVEL_##level < NIMBLE_SHIM_LOG_LEVEL) return; \
        va_list args; \
        va_start(args, format); \
        vprintf(format, args); \
        va_end(args); \
    }

#endif
