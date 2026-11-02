/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N32WB452 fsdev glue, adapted for this project:
 *  - elog dependency removed (logging goes through SEGGER RTT printf)
 *  - USB IRQ priorities raised to FreeRTOS-API-safe level (>= 5)
 */
#include "usbd_core.h"
#include "n32wb452.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "misc.h"

void usb_dc_low_level_init(void)
{
    RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV3);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USB, ENABLE);

    EXTI_InitType EXTI_InitStructure;

    /* Configure the EXTI line 18 connected internally to the USB IP */
    EXTI_ClrITPendBit(EXTI_LINE18);
    EXTI_InitStruct(&EXTI_InitStructure);
    EXTI_InitStructure.EXTI_Line = EXTI_LINE18;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&EXTI_InitStructure);

    /* Priority group 4 + configMAX_SYSCALL_INTERRUPT_PRIORITY=5: keep the USB
     * IRQs at preemption level 6 so ep callbacks may use FreeRTOS FromISR API. */
    NVIC_InitType NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = USBWakeUp_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // enable dp pull up res
    uint32_t reg_addr = 0x40001820;
    *((volatile uint32_t *)reg_addr) |= 1 << 28;
}

void usb_dc_low_level_deinit(void)
{
    // Disable USB Clock
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USB, DISABLE);

    // Disable Interrupt
    NVIC_InitType NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = USBWakeUp_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = DISABLE;
    NVIC_Init(&NVIC_InitStructure);

    // disable dp pull up res
    uint32_t reg_addr = 0x40001820;
    *((volatile uint32_t *)reg_addr) &= ~(1 << 28);
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    USBD_IRQHandler(0);
}

void USBWakeUp_IRQHandler(void)
{
    EXTI_ClrITPendBit(EXTI_LINE18);
}
