/*------------------------------------------------------------------------------
 * freertos_glue.c
 *
 * Board-specific FreeRTOS hooks for this example: fault/hang diagnostics
 * (configASSERT failure, stack overflow, malloc failure, CPU exceptions)
 * reported over THIS board's UART/LED. The chip-specific interrupt-
 * controller integration FreeRTOS itself requires (vConfigureTickInterrupt/
 * vClearTickInterrupt/vApplicationIRQHandler, VIM+RTI register access) has
 * moved to this toolchain repo's shared src/freertos_iwr68xx.c -- see that
 * file's header for why it isn't here or in Universal_hal. Nothing in
 * *this* file is chip-specific: it's plain calls through Universal_hal's
 * portable gpio/uart API, hardcoding only this board's LED pin (GPIO_PIN_2)
 * and console UART instance (MSS_SCIA) -- a different example on the same
 * chip could reuse freertos_iwr68xx.c directly but would still need its own
 * version of this file for its own pin choices.
 *----------------------------------------------------------------------------*/
#include <FreeRTOS.h>
#include <task.h>
#include <hal_gpio.h>
#include <hal_uart.h>
#include <string.h>

/* Diagnostic reporting for all the "silent hang" locations this port's
 * bring-up ran into (see FreeRTOSConfig.h's configMINIMAL_STACK_SIZE/
 * configASSERT comments) -- repeatedly prints a plain-text message over
 * UART (readable in any terminal, no counting fast blinks required) plus
 * a slow LED blink as a no-terminal-attached fallback signal. Plain
 * busy-wait, not vTaskDelay(): the scheduler is not assumed to be in a
 * usable state once any of these fires. uhal_uart_transmit() is a plain
 * polling register driver with no OS/scheduler dependency, safe to call
 * from any of these contexts (including the dedicated exception-mode
 * stack the fault handlers run on). */
static void diag_hook_delay(uint32_t count) {
    volatile uint32_t i;
    for (i = 0; i < count; i++) {
    }
}

static void diag_report(const char* msg) {
    const size_t len = strlen(msg);
    for (;;) {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)msg, len);
        diag_hook_delay(500000U);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        diag_hook_delay(1500000U);
    }
}

void vDiagAssertFail(void) {
    diag_report("configASSERT FAILED\r\n");
}

/* Called from startup_awr6843.S's Undefined/Prefetch-Abort placeholder
 * handlers instead of a silent infinite loop (Data Abort has its own,
 * more detailed vDiagDataAbort() below -- it's the one this port's
 * bring-up actually reproduced repeatedly). Runs on the dedicated
 * exception stack (_excStack) set up in _resetEntry, safe regardless of
 * what corrupted the interrupted code's own stack.
 * fault_id: 6=Undefined Instruction, 7=Prefetch Abort. */
void vDiagFaultHandler(uint32_t fault_id) {
    switch (fault_id) {
        case 6:
            diag_report("UNDEFINED INSTRUCTION\r\n");
            break;
        case 7:
            diag_report("PREFETCH ABORT\r\n");
            break;
        default:
            diag_report("UNKNOWN FAULT\r\n");
            break;
    }
}

static void diag_uart_hex32(uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    uint8_t buf[10];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = (uint8_t)hex_digits[(value >> (28 - (i * 4))) & 0xFU];
    }
    uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, buf, sizeof(buf));
}

/* Called from startup_awr6843.S's _dabortEntry with the exact faulting
 * address (DFAR), fault status (DFSR), and faulting instruction's own PC
 * (LR_abt - 8) -- see that file for the CP15 reads. Reports all three
 * over UART so the exact bad access can be cross-referenced against the
 * ELF's symbol table/disassembly, instead of just knowing "a Data Abort
 * happened somewhere in vTaskDelay()". */
void vDiagDataAbort(uint32_t dfar, uint32_t dfsr, uint32_t faulting_pc) {
    for (;;) {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)"DATA ABORT PC=", 14);
        diag_uart_hex32(faulting_pc);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)" DFAR=", 6);
        diag_uart_hex32(dfar);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)" DFSR=", 6);
        diag_uart_hex32(dfsr);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)"\r\n", 2);
        diag_hook_delay(500000U);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        diag_hook_delay(1500000U);
    }
}

void vApplicationMallocFailedHook(void) {
    diag_report("MALLOC FAILED\r\n");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    (void)xTask;
    /* pcTaskName tells us WHICH task overflowed -- report it directly
     * rather than just "a task overflowed". */
    for (;;) {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)"STACK OVERFLOW: ", 16);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)pcTaskName, strlen(pcTaskName));
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)"\r\n", 2);
        diag_hook_delay(500000U);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        diag_hook_delay(1500000U);
    }
}
