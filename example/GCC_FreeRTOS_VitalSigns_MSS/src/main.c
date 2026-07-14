/*
 * GCC_FreeRTOS_VitalSigns_MSS: a from-scratch MSS-side reimplementation of
 * a reference vital-signs-tracking demo's control/data-path architecture
 * (mailbox IPC protocol, sensorStart/Stop state machine, CLI-driven
 * per-feature config forwarding, detection-output UART relay), built on
 * this toolchain's own boot/HAL stack (same base as GCC_FreeRTOS_MSS --
 * see that project and src/startup_awr6843.S's header for the underlying
 * bring-up story) instead of the reference's SYS/BIOS + mmWave SDK stack.
 * See vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain memory notes
 * for the reference this mimics and the full scope-trim rationale.
 *
 * Deliberately out of scope, and why: the reference app's RF/chirp
 * configuration goes through mmWaveLink (a separate large TI library
 * driving the BSS/radar front end over SPI, with its own state machine
 * and calibration flow) -- porting that has no bearing on DSS-side
 * development, since DSS never talks to mmWaveLink directly, only to MSS
 * over the mailbox protocol this project *does* implement faithfully
 * (see mmw_messages.h, cli_vitalsigns.c, mbox_task.c). This project is a
 * testbench for exercising and developing that MSS<->DSS protocol/data
 * path -- not a working radar. sensorStart/sensorStop are local
 * MSS-state-only for the same reason (see cli_vitalsigns.c's header).
 *
 * Two FreeRTOS tasks, mirroring the two pieces of the reference's task
 * set that matter for this scope (see cli_vitalsigns.c/mbox_task.c
 * headers for the full mapping): vCliTask (command UART, MMWDEMO_CLI_*
 * equivalent) and vMboxReadTask (MmwDemo_mboxReadTask equivalent). The
 * reference's mmWaveCtrlTask (pumps MMWave_execute()) has no equivalent
 * here (no mmWaveLink); mssCtrlPathTask's only real job in the reference
 * beyond mmWaveLink dispatch was decoupling long-running RF calls from
 * the CLI task, which isn't needed here since sensorStart/Stop are
 * synchronous local-state flips -- folded directly into vCliTask instead
 * of kept as a separate task.
 */
#include <FreeRTOS.h>
#include <task.h>
#include <hal_pinmux.h>
#include <hal_gpio.h>
#include <hal_uart.h>
#include <hal_soc.h>
#include <hal_mailbox.h>
#include <string.h>
#include "cli_vitalsigns.h"
#include "mbox_task.h"

/* MSS_SYS_VCLK from the mmWave SDK's ti/common/sys_common_xwr68xx_mss.h --
 * see GCC_FreeRTOS_MSS/src/main.c's identical define for why this is
 * hardcoded rather than pulling in that header tree. Also the clock both
 * UART baud-rate dividers below and FreeRTOSConfig.h's configCPU_CLOCK_HZ
 * (the RTI tick-timer clock) assume -- keep in sync if this ever changes. */
#define MSS_SYS_VCLK 200000000U

static void boot_report(const char* msg) {
    (void)uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, (const uint8_t*)msg, strlen(msg));
}

#if DIAG_MINIMAL_BOOT
/* Byte-for-byte close to GCC_FreeRTOS_MSS's hardware-confirmed
 * vLedBlinkTask/vUartHelloTask (see that project's src/main.c) -- used
 * only to isolate whether a "flashed but silent, LED off, no UART" report
 * traces back to this project's own additions on top of that proven
 * boot path (UARTB pinmux/init, mailbox_init, the CLI/mailbox tasks) or
 * to something else entirely (bad flash, wrong SOP mode, etc). See
 * CMakeLists.txt's DIAG_MINIMAL_BOOT option. */
static void vLedBlinkTask(void* pvParameters) {
    gpio_level_t level = GPIO_LOW;
    (void)pvParameters;
    for (;;) {
        level = (level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
        gpio_set_pin_lvl(GPIO_PIN_2, level);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void vUartHelloTask(void* pvParameters) {
    static uint8_t message[] = "Hello World!\r\n";
    (void)pvParameters;
    for (;;) {
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, message, sizeof(message));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif /* DIAG_MINIMAL_BOOT */

int main(void) {
    if (soc_init() != UHAL_STATUS_OK) {
        /* No UART yet to report this over -- solid-off LED distinguishes
         * "never reached this point" from "soc_init() failed" during
         * bring-up debugging, same convention as GCC_FreeRTOS_MSS. */
        gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }

    /* Release DSS from its post-download halt as early as possible (see
     * hal_soc.h's soc_unhalt_dss() header comment and the AWR6843 TRM
     * s5.4.2 -- DSP power domain is OFF on POR, confirmed on real hardware
     * via a raw shared-memory alive-check in mbox_task.c before this call
     * existed: DSS never executed a single instruction). No UART up yet to
     * report a failure over -- treat it the same as a soc_init() failure
     * (solid-off LED) rather than silently continuing into a boot where
     * DSS can never respond. Harmless to call even for a flash image with
     * no real DSS program in it (e.g. still using a plain placeholder). */
    if (soc_unhalt_dss() != UHAL_STATUS_OK) {
        gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }

    /* Command UART (UARTA): N5/PADBE (U16) -> MSS_UARTA_TX, N4/PADBD (V16)
     * -> MSS_UARTA_RX -- identical wiring to every other example's single
     * UART, see e.g. GCC_FreeRTOS_MSS/src/main.c for the schematic
     * cross-check. Carries the CLI and human-readable status text. */
    pinmux_set_pin_function(MUX_PIN_U16, MUX_FUNC_PIN_U16_MSS_UART_TX);
    pinmux_set_pin_function(MUX_PIN_V16, MUX_FUNC_PIN_V16_MSS_UART_RX);

#if !DIAG_MINIMAL_BOOT
    /* Logging UART (UARTB): T3 -> MSS_UARTB_TX, U12 -> MSS_UARTB_RX --
     * matches the reference app's dual-UART split (command console vs.
     * binary detection-output stream, see mbox_task.c). Pin choice here
     * hasn't been cross-checked against a specific EVM schematic the way
     * U16/V16 was (see the comment above) -- may need adjusting for
     * whichever board's second UART you actually want to capture. */
    pinmux_set_pin_function(MUX_PIN_T3, MUX_FUNC_PIN_T3_MSS_UARTB_TX);
    pinmux_set_pin_function(MUX_PIN_U12, MUX_FUNC_PIN_U12_MSS_UARTB_RX);
#endif

    /* LED pinmux: K13/PADAZ (K3) -> GPIO_2, used only for the soc_init()
     * failure fallback above (same convention as every other example). */
    pinmux_set_pin_function(MUX_PIN_K3, MUX_FUNC_PIN_K3_GPIO2);
    gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);

    if (uhal_uart_init(UART_PERIPHERAL_MSS_SCIA, 115200U, UART_CLK_SOURCE_USE_DEFAULT, MSS_SYS_VCLK,
                        UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK) {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }

    /* Boot checkpoints on UARTA from here on -- this project has no LED
     * blink to confirm the scheduler/tick are alive the way GCC_FreeRTOS_MSS
     * does (see that project's vLedBlinkTask), so if a report ever comes
     * back as "flashed, but nothing at all on the command UART, not even
     * the CLI banner", these pin down which init step was last reached
     * (soc_init()'s own failure path above is the only step that still
     * only has the solid-off-LED signal, since UARTA isn't up yet then). */
    boot_report("\r\nBOOT: UARTA up\r\n");
    boot_report("BOOT: DSS unhalted\r\n");

#if DIAG_MINIMAL_BOOT
    boot_report("BOOT: DIAG_MINIMAL_BOOT -- starting scheduler\r\n");
    xTaskCreate(vLedBlinkTask, "led", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vUartHelloTask, "uart", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
#else
    if (uhal_uart_init(UART_PERIPHERAL_MSS_SCIB, 921600U, UART_CLK_SOURCE_USE_DEFAULT, MSS_SYS_VCLK,
                        UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK) {
        boot_report("BOOT: UARTB init FAILED\r\n");
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }
    boot_report("BOOT: UARTB up\r\n");

    if (mailbox_init(MAILBOX_PERIPHERAL_MSS_DSS) != UHAL_STATUS_OK) {
        boot_report("BOOT: mailbox init FAILED\r\n");
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1) {
        }
    }
    boot_report("BOOT: mailbox up\r\n");

    /* Same priority for both tasks (relying on configUSE_TIME_SLICING=1's
     * round-robin, like FreeRTOSConfig.h already documents this port
     * assumes) rather than giving vMboxReadTask a higher priority than
     * vCliTask: vMboxReadTask's poll loop only calls vTaskDelay() when
     * mailbox_message_pending() finds nothing waiting (see mbox_task.c) --
     * if DSS is (for whatever reason on its own end, e.g. retrying its own
     * mmWaveLink sync handshake against an MSS that never replies, since
     * this project doesn't implement that side) triggering the mailbox
     * doorbell back-to-back, a strictly-higher-priority vMboxReadTask would
     * never hit that vTaskDelay() and could starve vCliTask out of running
     * at all under FreeRTOS's priority-preemptive scheduler -- equal
     * priority means the CLI task is still guaranteed its time slice
     * either way. */
    boot_report("BOOT: starting scheduler\r\n");
    xTaskCreate(vCliTask, "cli", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(vMboxReadTask, "mbox", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
#endif /* DIAG_MINIMAL_BOOT */

    vTaskStartScheduler();

    /* Only reached if vTaskStartScheduler() itself failed (e.g. heap
     * exhausted creating the idle/timer tasks). */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
    for (;;) {
    }
}
