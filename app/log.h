/*
 * Firmware log macros → printf → SEGGER RTT (terminal 0).
 */
#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdio.h>

#ifndef APP_LOG_LEVEL
#define APP_LOG_LEVEL 1   /* 0=debug 1=info 2=warn 3=error */
#endif

#define LOGE(tag, fmt, ...) printf("E [%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) printf("W [%s] " fmt "\n", tag, ##__VA_ARGS__)

#if APP_LOG_LEVEL <= 1
#define LOGI(tag, fmt, ...) printf("I [%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOGI(tag, fmt, ...) ((void)0)
#endif

#if APP_LOG_LEVEL == 0
#define LOGD(tag, fmt, ...) printf("D [%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOGD(tag, fmt, ...) ((void)0)
#endif

#endif /* APP_LOG_H */
