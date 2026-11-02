/*
 * Shared Mynewt-log shim: routes NimBLE / Nations SDK logging to printf
 * (retargeted to SEGGER RTT by app/syscalls.c).
 */
#ifndef N32_LOG_SHIM_H
#define N32_LOG_SHIM_H

#include <stdio.h>

#define LOG_LEVEL_DEBUG     (0)
#define LOG_LEVEL_INFO      (1)
#define LOG_LEVEL_WARN      (2)
#define LOG_LEVEL_ERROR     (3)
#define LOG_LEVEL_CRITICAL  (4)

/* Compile-time floor: 0=all, 1 silences DEBUG, 2 silences INFO as well. */
#ifndef NIMBLE_SHIM_LOG_LEVEL
#define NIMBLE_SHIM_LOG_LEVEL 2
#endif

static inline const char *n32_log_lvl_str(int lvl)
{
    switch (lvl) {
    case LOG_LEVEL_DEBUG:    return "D";
    case LOG_LEVEL_INFO:     return "I";
    case LOG_LEVEL_WARN:     return "W";
    case LOG_LEVEL_ERROR:    return "E";
    default:                 return "!";
    }
}

#define MODLOG_DFLT(lvl, ...) \
    do { if ((lvl) >= NIMBLE_SHIM_LOG_LEVEL) { \
        printf("[%s] ", n32_log_lvl_str(lvl)); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } } while (0)

#define MODLOG(mod, lvl, ...) MODLOG_DFLT(lvl, __VA_ARGS__)

/* console stub used by porting layer / host */
#define console_printf printf

/* Nations SDK Interface.c ble_log(); BLE_DEBUG may already come from
 * rwble_config.h when the RW headers are in scope. */
#ifndef BLE_DEBUG
#define BLE_DEBUG 0
#endif
#define ble_log(lvl, fmt, ...) \
    do { if ((lvl) >= NIMBLE_SHIM_LOG_LEVEL) { \
        printf("[ble.%s] ", n32_log_lvl_str(LOG_LEVEL_DEBUG)); \
        printf(fmt, ##__VA_ARGS__); \
        printf("\n"); \
    } } while (0)

#endif /* N32_LOG_SHIM_H */
