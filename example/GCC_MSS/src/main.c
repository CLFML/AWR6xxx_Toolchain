/*
*  GCC MSS example: same bare-metal (no SYS/BIOS) boot as Barebones_MSS,
*  built with arm-none-eabi-gcc instead of TI's armcl/armlnk -- a parallel
*  toolchain choice, not a fix for anything wrong with the TI build. See
*  Barebones_MSS's own git history and the AWR6xxx_Toolchain memory notes
*  for why this project's bare-metal reset trampoline needs to explicitly
*  set up the Abort/Undefined-mode banked stack pointers and reset
*  VIM/clear ESM before main() runs (src/startup_awr6843.S's file header
*  has the full story) -- none of that changes with the compiler, only
*  the assembler syntax it's written in does.
*
*  This file itself is unchanged from Barebones_MSS's main.c: it's plain
*  portable C against Universal_hal's API, no TI- or GCC-specific code.
*
*  No RTOS: this is a plain superloop, not a SYS/BIOS Task. Universal_hal's
*  esm_init() isn't used here (it needs SYS/BIOS's Hwi_create() to install
*  the ESM FIQ/IRQ handlers) -- the startup trampoline does its own
*  register-level ESM clear instead, matching what SYS/BIOS's Hwi module
*  does at the same point in boot.
*
*  UART pin mapping, cross-checked against the AWR6843AOPEVM schematic
*  (PROC091G), the mmWave SDK's pinmux_xwr68xx.h, and a firmware project
*  independently confirmed booting on this exact board:
*     device pad "N5/PADBE" (== ball U16 on the AOP package) -> MSS_UARTA_TX
*     device pad "N4/PADBD" (== ball V16 on the AOP package) -> MSS_UARTA_RX
*  These pads are wired on the EVM to the CP2105 USB-UART bridge (the same
*  bridge UniFlash uses to flash this image), so the output shows up on the
*  "Application/User UART" COM port at 115200 8N1.
*
*  The GPIO_2 LED pin (K13/PADAZ, == ball K3 on the AOP package) is the same
*  pin the mmWave out-of-box demo uses for its sensor-status LED.
*/
#include <hal_pinmux.h>
#include <hal_gpio.h>
#include <hal_uart.h>
#include <hal_soc.h>

/* MSS_SYS_VCLK from the mmWave SDK's ti/common/sys_common_xwr68xx_mss.h --
 * hardcoded rather than pulling in that header tree (which this project
 * otherwise has no dependency on): soc_init() brings VCLK up to exactly
 * this frequency, so this is that same fixed value, not a guess. */
#define MSS_SYS_VCLK 200000000U

static void delay(volatile uint32_t count)
{
    while (count--)
    {
    }
}

int main(void)
{
    if (soc_init() != UHAL_STATUS_OK)
    {
        /* No UART yet to report this over -- solid-off LED distinguishes
         * "never reached this point" from "soc_init() failed" during
         * bring-up debugging. */
        gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1)
        {
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
                        MSS_SYS_VCLK, UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK)
    {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1)
        {
        }
    }

    uint8_t message[] = "Hello World!\r\n";
    gpio_level_t level = GPIO_LOW;
    while (1)
    {
        level = (level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
        gpio_set_pin_lvl(GPIO_PIN_2, level);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, message, sizeof(message));
        delay(4000000U);
    }

    return 0;
}
