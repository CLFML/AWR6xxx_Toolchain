/*
 * GCC_FreeRTOS_DSS_Probe_MSS: same boot/peripheral bring-up as
 * GCC_FreeRTOS_MSS (see that project's main.c and src/startup_awr6843.S
 * for the full story on the reset trampoline's Abort/Undefined-mode
 * stacks + VIM/ESM setup), but this project's own task is
 * cli_dss_probe.c's UART command console instead of a demo mailbox
 * protocol -- see that file's header for the full command list and
 * intended workflow ("use MSS as a debug probe/flasher for DSS, no JTAG
 * required").
 *
 * Deliberately does NOT call soc_unhalt_dss() here at boot, unlike every
 * other example in this toolchain that talks to DSS -- this project's
 * whole point is to let you inspect/overwrite DSS's memory (dssPeek/
 * dssPoke/dssLoad) BEFORE releasing it from its power-on halt, so `dssLoad`
 * followed by `dssStart` from the CLI is how DSS actually starts running
 * here, not something main() decides for you.
 *
 * UART pin mapping and LED pin: identical to every other MSS example, see
 * Barebones_MSS/src/main.c for the schematic cross-check.
 */
#include <FreeRTOS.h>
#include <task.h>
#include <hal_pinmux.h>
#include <hal_gpio.h>
#include <hal_uart.h>
#include <hal_soc.h>
#include "cli_dss_probe.h"
#include "dss_mem.h"

/* MSS_SYS_VCLK from the mmWave SDK's ti/common/sys_common_xwr68xx_mss.h --
 * see GCC_FreeRTOS_MSS/src/main.c's identical comment for why this is
 * hardcoded rather than pulling in that header tree. Also what
 * FreeRTOSConfig.h's configCPU_CLOCK_HZ assumes -- keep in sync. */
#define MSS_SYS_VCLK 200000000U

static void vLedHeartbeatTask(void* pvParameters) {
    gpio_level_t level = GPIO_LOW;
    (void)pvParameters;
    for (;;) {
        level = (level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
        gpio_set_pin_lvl(GPIO_PIN_2, level);
        vTaskDelay(pdMS_TO_TICKS(1000));
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

    /* See dss_mem.h's header for why this is needed: mpu_config() (inside
     * soc_init() above) has no MPU region open for DSS's L2 SRAM alias, so
     * dssPeek/dssPoke/dssLoad would data-abort MSS itself without this.
     * Must run before the scheduler starts (no concurrent tasks/interrupts
     * yet) since it briefly disables the MPU entirely. */
    dss_mem_mpu_open_l2();

    /* UART pinmux: N5/PADBE (U16) -> MSS_UARTA_TX, N4/PADBD (V16) -> MSS_UARTA_RX */
    pinmux_set_pin_function(MUX_PIN_U16, MUX_FUNC_PIN_U16_MSS_UART_TX);
    pinmux_set_pin_function(MUX_PIN_V16, MUX_FUNC_PIN_V16_MSS_UART_RX);

    /* LED pinmux: K13/PADAZ (K3) -> GPIO_2 */
    pinmux_set_pin_function(MUX_PIN_K3, MUX_FUNC_PIN_K3_GPIO2);

    gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);

    /* Diagnostic value: light the LED solid before touching UART -- same
     * as GCC_FreeRTOS_MSS, distinguishes "never reached this task" from
     * "stuck in UART" if uhal_uart_init() below were ever to hang. */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);

    if (uhal_uart_init(UART_PERIPHERAL_MSS_SCIA, 115200U, UART_CLK_SOURCE_USE_DEFAULT,
                        MSS_SYS_VCLK, UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK) {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }

    xTaskCreate(vLedHeartbeatTask, "led", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vCliTask, "cli", configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    /* Only reached if vTaskStartScheduler() itself failed (e.g. heap
     * exhausted creating the idle/timer tasks). */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
    for (;;) {
    }
}
