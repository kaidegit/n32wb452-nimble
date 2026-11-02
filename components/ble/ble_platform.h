/*
 * BLE platform support for the closed-source NZ8801 driver archive
 * (libn32wb452_ble.a) + SDK platform sources (Interface.c / Eif_*.c).
 */
#ifndef BLE_PLATFORM_H
#define BLE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "n32wb452.h"
#include "n32wb452_tim.h"
#include "n32wb452_rcc.h"

/* Status word read by the closed driver. */
extern volatile uint32_t gBT_STS;

/* TIM3 helpers used by SDK Eif_timer.c */
void TIM3_config(uint16_t arr, uint16_t psc);
void TIM3_IRQ_enable(uint8_t flag);
void TIM3_set_timeout(uint32_t count);
uint32_t TIM3_get_time(void);

/* Initialize SPI/GPIO with the controller held in reset (bridge boot). */
void ble_controller_prepare(void);

/* Fixed BLE init chain (Interface.c). */
int32_t ble_interface_init(void);
int32_t ble_hardware_init(void);
int32_t ble_initdata_down(void);

#endif /* BLE_PLATFORM_H */
