/* ble_log() for the Nations BLE driver sources (Interface.c et al.). */
#ifndef __N32WB452_LOG_LEVEL_H__
#define __N32WB452_LOG_LEVEL_H__

#include <stdio.h>

#define BLE_NO_LOG      0
#define BLE_DBG         1
#define BLE_INFO        2
#define BLE_WARNING     3
#define BLE_ERROR       4
#define BLE_FAULT       5
#define BLE_LOG_LIMIT   6

/* Route driver logs to printf (SEGGER RTT). */
#define ble_log(lvl, fmt, ...) printf("[ble] " fmt "\r\n", ##__VA_ARGS__)

#endif
