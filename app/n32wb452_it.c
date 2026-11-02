/*
 * Interrupt service routines used by this firmware:
 *   - EXTI15_10: NZ8801 data-valid IRQ (PE14) → BLE transport
 *   - TIM3: legacy RW timer tick (ke_event_set stub; harmless)
 *
 * SVC/PendSV/SysTick handlers come from the FreeRTOS ARM_CM4F port via the
 * #define mappings in FreeRTOSConfig.h (no wrappers: the port validates the
 * vector table entries). USB LP / USBWakeUp handlers live in the CherryUSB
 * fsdev glue file.
 */
#include "n32wb452_it.h"
#include "n32wb452_exti.h"
#include "n32wb452_tim.h"
#include "SEGGER_RTT.h"
#include "FreeRTOSConfig.h"

/* Provided by the BLE transport (both firmware modes). */
extern void hci_spi_irq_handler(void);

void EXTI15_10_IRQHandler(void)
{
    /* NZ8801 data-valid line is PE14 (EXTI_LINE14). Clear before dispatch,
     * matching the vendor driver's expectation (level re-asserts if more
     * data is pending). */
    if (EXTI_GetITStatus(EXTI_LINE14) != RESET) {
        EXTI_ClrITPendBit(EXTI_LINE14);
        hci_spi_irq_handler();
    }
}

/* TIM3 services eif_timer_isr() from the SDK platform layer. */
extern void eif_timer_isr(void);
void TIM3_IRQHandler(void)
{
    if (TIM_GetIntStatus(TIM3, TIM_INT_UPDATE) != RESET) {
        TIM_ClrIntPendingBit(TIM3, TIM_INT_UPDATE);
        eif_timer_isr();
    }
}

void app_panic(const char *msg)
{
    __disable_irq();
    SEGGER_RTT_WriteString(0, "\r\n*** PANIC: ");
    SEGGER_RTT_WriteString(0, msg ? msg : "?");
    SEGGER_RTT_WriteString(0, " ***\r\n");
    while (1) {
        __NOP();
    }
}

/* Capture EXC_RETURN and the hardware stack before a C prologue changes
 * LR/MSP. Core registers come first in both basic and extended FPU frames. */
static void fault_puts(const char *s)
{
    unsigned len = 0;
    while (s[len]) { ++len; }
    /* Never wait for a debugger to drain RTT while handling a fault. */
    SEGGER_RTT_WriteNoLock(0, s, len);
}

static void fault_hex(const char *name, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char hex[11];
    for (unsigned i = 0; i < 8; ++i) {
        hex[i] = digits[(value >> (28 - 4 * i)) & 0xf];
    }
    hex[8] = '\r';
    hex[9] = '\n';
    hex[10] = 0;
    fault_puts(name);
    fault_puts(hex);
}

__attribute__((used, noinline, noreturn))
static void fault_dump(const uint32_t *stack, uint32_t exc_return, uint32_t kind)
{
    extern uint32_t _estack;
    static const char *const names[] = {
        "HardFault", "MemManage", "BusFault", "UsageFault"
    };
    static const char *const regs[] = {
        "r0=", "r1=", "r2=", "r3=", "r12=", "lr=", "pc=", "xpsr="
    };
    const uint32_t cfsr = SCB->CFSR;
    const uint32_t hfsr = SCB->HFSR;
    const uint32_t bfar = SCB->BFAR;
    const uint32_t mmfar = SCB->MMFAR;
    const uintptr_t frame_addr = (uintptr_t)stack;

    __disable_irq();
    if (_SEGGER_RTT.acID[0] != 'S') {
        SEGGER_RTT_Init();
    }
    _SEGGER_RTT.aUp[0].Flags = SEGGER_RTT_MODE_NO_BLOCK_SKIP;
    fault_puts("\r\n*** ");
    fault_puts(names[kind]);
    fault_puts(" ***\r\n");
    fault_hex("CFSR=", cfsr);
    fault_hex("HFSR=", hfsr);
    fault_hex("BFAR=", bfar);
    fault_hex("MMFAR=", mmfar);
    fault_hex("EXC_RETURN=", exc_return);
    fault_hex("SP=", frame_addr);

    /* Stacking errors leave the frame incomplete. Do not dereference it. */
    if (!(cfsr & ((1U << 4) | (1U << 12))) &&
        frame_addr >= SRAM_BASE && !(frame_addr & 3U) &&
        frame_addr <= (uintptr_t)&_estack - 8 * sizeof(uint32_t)) {
        const uint32_t *frame = (const uint32_t *)frame_addr;
        for (unsigned i = 0; i < 8; ++i) {
            fault_hex(regs[i], frame[i]);
        }
    } else {
        fault_puts("exception frame unavailable\r\n");
    }
    while (1) { __NOP(); }
}

#define FAULT_HANDLER(name, kind)                    \
    __attribute__((naked)) void name(void)           \
    {                                               \
        __asm volatile ("tst lr, #4\n"               \
                        "ite eq\n"                   \
                        "mrseq r0, msp\n"            \
                        "mrsne r0, psp\n"            \
                        "mov r1, lr\n"               \
                        "movs r2, #" #kind "\n"       \
                        "b fault_dump\n");            \
    }

void NMI_Handler(void) { app_panic("NMI"); }
FAULT_HANDLER(HardFault_Handler, 0)
FAULT_HANDLER(MemManage_Handler, 1)
FAULT_HANDLER(BusFault_Handler, 2)
FAULT_HANDLER(UsageFault_Handler, 3)
