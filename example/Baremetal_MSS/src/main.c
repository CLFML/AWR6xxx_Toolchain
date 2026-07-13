/*
*  Baremetal MSS example: third attempt at a bare-metal (no SYS/BIOS) boot
*  on the AWR6843AOP, this time explicitly replicating the two pieces of
*  hardware bring-up SYS/BIOS's own generated startup does that a plain TI
*  RTS _c_int00 does not -- see src/startup_awr6843.asm's file header and
*  the AWR6xxx_Toolchain memory note on xdctools/configuro's generated
*  output for the full story. Earlier attempts (see Barebones_MSS's git
*  history) hit a reliable data abort on the first MSS peripheral register
*  access after boot; this one exists specifically to test whether
*  initializing the Abort/Undefined-mode banked stack pointers (and
*  resetting VIM/clearing ESM) before main() runs fixes that.
*
*  No RTOS: this is a plain superloop, not a SYS/BIOS Task. Universal_hal's
*  esm_init() isn't used here (it needs SYS/BIOS's Hwi_create() to install
*  the ESM FIQ/IRQ handlers) -- the startup trampoline does its own
*  register-level ESM clear instead, matching what SYS/BIOS's Hwi module
*  does at the same point in boot.
*
*  Same UART/LED pin mapping as Barebones_MSS -- see that project's main.c
*  for the schematic cross-reference.
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

    /* Diagnostic value: light the LED solid before touching UART, same
     * convention as Barebones_MSS -- distinguishes "never reached this
     * point" from "stuck in UART" if something goes wrong from here. */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);

    if (uhal_uart_init(UART_PERIPHERAL_MSS_SCIA, 115200U, UART_CLK_SOURCE_USE_DEFAULT,
                        MSS_SYS_VCLK, UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK)
    {
        gpio_set_pin_lvl(GPIO_PIN_2, GPIO_LOW);
        while (1)
        {
        }
    }

    uint8_t message[] = "Hello World! (bare-metal)\r\n";
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
