#ifndef PERF_CLOCK_N32_H
#define PERF_CLOCK_N32_H

/* Reserve TIM2 for the independent 1 MHz GATT benchmark clock. */
void gatt_perf_clock_init(void);

#endif
