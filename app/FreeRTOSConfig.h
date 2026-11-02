#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* N32WB452 (Cortex-M4F @144 MHz) + NimBLE host + USB CDC bridge. */

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (144000000U)
#define configSYSTICK_CLOCK_HZ                  (configCPU_CLOCK_HZ)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    (16)
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                   ((size_t)(56 * 1024))
#define configMAX_TASK_NAME_LEN                 (16)
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/* Memory allocation: heap_4 */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configAPPLICATION_ALLOCATED_HEAP        0

/* API inclusion: what the kernel actually compiles in */
#define INCLUDE_vTaskDelay                 1
#define INCLUDE_vTaskDelete                1
#define INCLUDE_vTaskSuspend               1
#define INCLUDE_vTaskPrioritySet           0
#define INCLUDE_vTaskPriorityGet           0
#define INCLUDE_vTaskCleanUpResources      0
#define INCLUDE_vTaskGetInfo               0
#define INCLUDE_vTaskGetSchedulerState     1
#define INCLUDE_uxTaskPriorityGet          0
#define INCLUDE_uxTaskGetStackHighWaterMark 0
#define INCLUDE_uxTaskGetSystemState       0
#define INCLUDE_xTaskGetSchedulerState     1
#define INCLUDE_xTaskGetCurrentTaskHandle  1
#define INCLUDE_xTaskGetIdleTaskHandle     0
#define INCLUDE_xTaskAbortDelay            0
#define INCLUDE_xTaskGetHandle             0
#define INCLUDE_xTaskResumeFromISR         0
#define INCLUDE_xTimerPendFunctionCall     1
#define INCLUDE_eTaskGetState              1

/* Hook functions */
#define configCHECK_FOR_STACK_OVERFLOW         2
#define configUSE_MALLOC_FAILED_HOOK           1
#define configUSE_IDLE_HOOK                    0
#define configUSE_TICK_HOOK                    0
#define configUSE_DAEMON_TASK_STARTUP_HOOK     0

/* Run time stats: off */
#define configGENERATE_RUN_TIME_STATS          0
#define configUSE_TRACE_FACILITY               0
#define configUSE_STATS_FORMATTING_FUNCTIONS   0

/* Software timers */
#define configUSE_TIMERS                       1
#define configTIMER_TASK_PRIORITY              (2)
#define configTIMER_QUEUE_LENGTH               16
#define configTIMER_TASK_STACK_DEPTH           (256)

/* Co-routines: off */
#define configUSE_CO_ROUTINES                  0
#define configMAX_CO_ROUTINE_PRIORITIES        (2)

/* Map the FreeRTOS port handlers onto the CMSIS vector names. The ARM_CM4F
 * port validates that the vector table entries point at these symbols
 * exactly, so no wrapper functions may sit in between. */
#define vPortSVCHandler        SVC_Handler
#define xPortPendSVHandler     PendSV_Handler
#define xPortSysTickHandler    SysTick_Handler

/* Debugging: assertions land in app_panic (RTT banner + halt) */
#define configASSERT(x) \
    do { if (!(x)) { taskDISABLE_INTERRUPTS(); app_panic("FreeRTOS assert"); } } while (0)

/* NVIC: priority group 4 (all 4 bits preempt), ISR-safe level 5 */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                        __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                        4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#include <stdint.h>
extern void app_panic(const char *msg);

#endif /* FREERTOS_CONFIG_H */
