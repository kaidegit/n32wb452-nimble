/*
 * Mynewt-log shim for the PC host: NimBLE logs go to stderr so stdout
 * stays clean for command output (e.g. scan lines).
 */
#ifndef BLECTL_LOG_SHIM_H
#define BLECTL_LOG_SHIM_H

#include <stdio.h>

#define LOG_LEVEL_DEBUG     (0)
#define LOG_LEVEL_INFO      (1)
#define LOG_LEVEL_WARN      (2)
#define LOG_LEVEL_ERROR     (3)
#define LOG_LEVEL_CRITICAL  (4)

#ifndef BLECTL_NIMBLE_LOG_LEVEL
#define BLECTL_NIMBLE_LOG_LEVEL LOG_LEVEL_WARN
#endif

static inline const char *blectl_lvl_str(int lvl)
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
    do { if ((lvl) >= BLECTL_NIMBLE_LOG_LEVEL) { \
        fprintf(stderr, "[nimble.%s] ", blectl_lvl_str(lvl)); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } } while (0)
#define MODLOG(mod, lvl, ...) MODLOG_DFLT(lvl, __VA_ARGS__)

#define console_printf printf

#endif /* BLECTL_LOG_SHIM_H */
