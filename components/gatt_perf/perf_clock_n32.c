/* Independent 1 MHz wall clock for receiver-side benchmark timing. TIM3
 * belongs to the vendor driver; TIM2 is otherwise unused by this firmware. */
#include <stdint.h>
#include "perf_clock_n32.h"
#include "gatt_perf.h"
#include "n32wb452.h"
#include "n32wb452_rcc.h"
#include "n32wb452_tim.h"

static volatile uint32_t overflow_count;

void gatt_perf_clock_init(void)
{
    RCC_ClocksType clocks;
    RCC_GetClocksFreqValue(&clocks);
    /* APB timers run at twice PCLK when the APB prescaler is not one. */
    uint32_t timer_hz = clocks.Pclk1Freq;
    if (clocks.Pclk1Freq != clocks.HclkFreq) timer_hz *= 2;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM2, ENABLE);
    TIM_DeInit(TIM2);
    TIM_TimeBaseInitType config = {0};
    config.Period = UINT16_MAX;
    config.Prescaler = timer_hz / 1000000u - 1;
    config.ClkDiv = TIM_CLK_DIV1;
    config.CntMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM2, &config);
    overflow_count = 0;
    TIM_ClrIntPendingBit(TIM2, TIM_INT_UPDATE);
    NVIC_SetPriority(TIM2_IRQn, 6);
    TIM_ConfigInt(TIM2, TIM_INT_UPDATE, ENABLE);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM_Enable(TIM2, ENABLE);
}

void TIM2_IRQHandler(void)
{
    if (TIM_GetIntStatus(TIM2, TIM_INT_UPDATE) != RESET) {
        TIM_ClrIntPendingBit(TIM2, TIM_INT_UPDATE);
        overflow_count++;
    }
}

uint32_t gatt_perf_time_us(void)
{
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    uint32_t high = overflow_count;
    uint32_t low = TIM2->CNT;
    /* Account for an overflow whose ISR has not run yet, including one
     * between the first counter read and the pending-flag read. */
    if (TIM_GetIntStatus(TIM2, TIM_INT_UPDATE) != RESET) {
        high++;
        low = TIM2->CNT;
    }
    __set_PRIMASK(interrupt_mask);
    return (high << 16) | low;
}
