/*
 * Platform glue for the Nations BLE driver:
 *  - TIM3 (the RW stack's 10 ms tick; IRQ stays off unless the closed host
 *    would enable it — NimBLE uses os_callout instead)
 *  - RW host symbols referenced by Interface.c that would normally live in
 *    the closed host archive (app_env, ke_event_set, ...)
 *  - gBT_STS status word referenced by libn32wb452_ble.a
 */
#include <stdint.h>
#include <stdbool.h>
#include "ble_platform.h"
#include "Eif_iom.h"
#include "Eif_spi.h"
#include "Eif_timer.h"
#include "app.h"          /* struct app_env_tag (RW profile header) */
#include "ke_event.h"
#include "n32wb452_ble_api.h"   /* bt_attr_param for g_bt_init */

/* ---- status word for the closed driver ---- */
volatile uint32_t gBT_STS = 0;

/* ---- RW host objects Interface.c links against ---- */
struct app_env_tag app_env;

/* bt control block Interface.c reads; NULL keeps it on the default path. */
bt_attr_param *g_bt_init = NULL;
uint32_t g_bt_start_finished_flag = 0;

/* eif_timer_isr() (Eif_timer.c) calls into the RW kernel; with the closed
 * host replaced by NimBLE there is no kernel event to raise. Signature per
 * ke_event.h. */
void ke_event_set(uint8_t event_type) { (void)event_type; }

/* Defensive: these live in the closed host archive we deliberately do not
 * link. Interface.c references them only from ble_host_init(), which is
 * never called; stubs keep the link deterministic without --gc-sections.
 * Signatures per n32wb452_ble_api.h. */
void BT_init(void)   { }
void appm_init(void) { }
void BT_handle(void) { }

/* ---- TIM3 ---- */
void TIM3_config(uint16_t arr, uint16_t psc)
{
    TIM_TimeBaseInitType tim_init;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM3, ENABLE);

    TIM_DeInit(TIM3);
    tim_init.Period    = arr;
    tim_init.Prescaler = psc;
    tim_init.ClkDiv    = TIM_CLK_DIV1;
    tim_init.CntMode   = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM3, &tim_init);

    TIM_Enable(TIM3, ENABLE);
}

void TIM3_IRQ_enable(uint8_t flag)
{
    if (flag) {
        TIM_ConfigInt(TIM3, TIM_INT_UPDATE, ENABLE);
        NVIC_EnableIRQ(TIM3_IRQn);
    } else {
        NVIC_DisableIRQ(TIM3_IRQn);
        TIM_ConfigInt(TIM3, TIM_INT_UPDATE, DISABLE);
    }
}

void TIM3_set_timeout(uint32_t count)
{
    TIM3->CNT = 0;
    TIM3->AR  = count;
}

uint32_t TIM3_get_time(void)
{
    return TIM3->CNT;
}

/* Bridge needs a working SPI bus while the controller stays in reset.
 * Unlike ble_hardware_init(), this does not pulse/release the reset pin. */
void ble_controller_prepare(void)
{
    ble_interface_init();
    eif_spi_recv_irq_enable(0);
    eif_gpio_init();
    eif_pull_down_reset_io();
    eif_timer_init();
    eif_spi_init();
    eif_pull_down_reset_io();
    NVIC_SetPriorityGrouping(3); /* FreeRTOS: restore Group_4 after vendor IO setup. */
}
