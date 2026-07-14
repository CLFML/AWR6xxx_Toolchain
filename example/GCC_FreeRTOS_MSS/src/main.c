/*
*  GCC_FreeRTOS_MSS example: same boot/peripheral bring-up as GCC_MSS (see
*  that project's main.c and src/startup_awr6843.S for the full story on
*  why the reset trampoline needs to explicitly set up Abort/Undefined-mode
*  stacks and reset VIM/clear ESM before main() runs), plus FreeRTOS-Kernel
*  running two independent tasks instead of one combined superloop -- see
*  ../../src/freertos_iwr68xx.c (this toolchain's shared VIM+RTI tick-timer
*  integration, not local to this project) for the interrupt-controller
*  glue this needs. CONFIRMED WORKING on real hardware -- see the
*  AWR6xxx_Toolchain memory notes (gcc_freertos_mss_port.md) for the full
*  bring-up story, including a real bug (RTIA/RTIB mixup) found and fixed
*  along the way.
*
*  UART pin mapping and LED pin: identical to GCC_MSS/Barebones_MSS, see
*  those projects' main.c for the schematic cross-check.
*/
#include <FreeRTOS.h>
#include <task.h>
#include <hal_pinmux.h>
#include <hal_gpio.h>
#include <hal_uart.h>
#include <hal_soc.h>

/* MSS_SYS_VCLK from the mmWave SDK's ti/common/sys_common_xwr68xx_mss.h --
 * hardcoded rather than pulling in that header tree (which this project
 * otherwise has no dependency on): soc_init() brings VCLK up to exactly
 * this frequency, so this is that same fixed value, not a guess. Also the
 * clock FreeRTOSConfig.h's configCPU_CLOCK_HZ (the RTI tick-timer clock)
 * assumes -- keep the two in sync if this ever changes. */
#define MSS_SYS_VCLK 200000000U

/* DIAG_TASKYIELD/DIAG_SUSPEND_RESUME/DIAG_SKIP_SCHEDULER: a reusable
 * bisection harness (all OFF by default, zero effect on a normal build),
 * kept in the tree for any future "flashed but silent" debugging on this
 * port rather than deleted now that the bug that motivated it is fixed
 * (see gcc_freertos_mss_port.md in the AWR6xxx_Toolchain memory notes for
 * the full story -- the real cause turned out to be RTIA vs RTIB, an
 * unrelated register-address mixup, not the scheduler/SVC mechanism this
 * harness was built to isolate). DIAG_TASKYIELD: isolate FreeRTOS's
 * SVC/context-switch mechanism (portYIELD()/FreeRTOS_SVC_Handler/
 * vTaskSwitchContext) from vTaskDelay()'s delayed-list + tick-wake
 * bookkeeping -- a busy-loop stands in for the delay, and an explicit
 * taskYIELD() forces the same SVC-triggered switch vTaskDelay() would. */
#if DIAG_TASKYIELD || DIAG_SUSPEND_RESUME
static void diag_busy_delay(void) {
    for (volatile uint32_t i = 0; i < 2000000U; i++) {
    }
}
#endif

static void vLedBlinkTask(void* pvParameters) {
    gpio_level_t level = GPIO_LOW;
    (void)pvParameters;
    for (;;) {
        level = (level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
        gpio_set_pin_lvl(GPIO_PIN_2, level);
#if DIAG_SUSPEND_RESUME
        /* Isolates vTaskSuspendAll()/xTaskResumeAll() (used by vTaskDelay())
         * from delayed-task-list insertion (also used by vTaskDelay(), but
         * NOT exercised here) -- see this block's twin below and
         * gcc_freertos_mss_port.md in the AWR6xxx_Toolchain memory notes
         * for the full bisection story. */
        diag_busy_delay();
        vTaskSuspendAll();
        (void)xTaskResumeAll();
        taskYIELD();
#elif DIAG_TASKYIELD
        diag_busy_delay();
        taskYIELD();
#else
        vTaskDelay(pdMS_TO_TICKS(500));
#endif
    }
}

static void vUartHelloTask(void* pvParameters) {
    static uint8_t message[] = "Hello World!\r\n";
    (void)pvParameters;
    for (;;) {
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, message, sizeof(message));
#if DIAG_SUSPEND_RESUME
        diag_busy_delay();
        vTaskSuspendAll();
        (void)xTaskResumeAll();
        taskYIELD();
#elif DIAG_TASKYIELD
        diag_busy_delay();
        taskYIELD();
#else
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }
}

int main(void) {
    if (soc_init() != UHAL_STATUS_OK) {
        /* No UART yet to report this over -- solid-off LED distinguishes
         * "never reached this point" from "soc_init() failed" during
         * bring-up debugging. */
        gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }

    /* UART pinmux: N5/PADBE (U16) -> MSS_UARTA_TX, N4/PADBD (V16) -> MSS_UARTA_RX */
    pinmux_set_pin_function(MUX_PIN_U16, MUX_FUNC_PIN_U16_MSS_UART_TX);
    pinmux_set_pin_function(MUX_PIN_V16, MUX_FUNC_PIN_V16_MSS_UART_RX);

    /* LED pinmux: K13/PADAZ (K3) -> GPIO_2 */
    pinmux_set_pin_function(MUX_PIN_K3, MUX_FUNC_PIN_K3_GPIO2);

    gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);

    /* Diagnostic value: light the LED solid before touching UART. If
     * uhal_uart_init() below were ever to hang, this distinguishes "never
     * reached this task" from "stuck in UART". */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);

    if (uhal_uart_init(UART_PERIPHERAL_MSS_SCIA, 115200U, UART_CLK_SOURCE_USE_DEFAULT,
                        MSS_SYS_VCLK, UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK) {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }

#if DIAG_SKIP_SCHEDULER
    /* Bisection build: everything still links against FreeRTOS-Kernel
     * (same binary size/.bss/heap_4 footprint as the real build), but
     * never calls vTaskStartScheduler() -- plain superloop instead, like
     * GCC_MSS. Part of the reusable DIAG_* harness (see the comment above
     * vLedBlinkTask/vUartHelloTask) -- kept for any future bisection need,
     * not because anything is currently broken. */
    uint8_t message[] = "Hello World!\r\n";
    gpio_level_t level = GPIO_LOW;
    while (1) {
        level = (level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
        gpio_set_pin_lvl(GPIO_PIN_2, level);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, message, sizeof(message));
        for (volatile uint32_t i = 0; i < 4000000U; i++) {
        }
    }
#else
    /* Two independent tasks instead of one combined loop -- LED toggles
     * every 500ms, UART message every 1000ms, on their own schedules
     * rather than sharing a single delay. If both are observed running
     * concurrently (LED blinking twice as often as messages print), that
     * confirms the scheduler/tick/context-switch path is actually working,
     * not just that main() reached vTaskStartScheduler(). */
    xTaskCreate(vLedBlinkTask, "led", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vUartHelloTask, "uart", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    /* Only reached if vTaskStartScheduler() itself failed (e.g. heap
     * exhausted creating the idle/timer tasks). */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
    for (;;) {
    }
#endif
}
