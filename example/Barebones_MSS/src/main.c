/*
*  Copyright 2024 (C) Jeroen Veen <ducroq> & Victor Hogeweij <Hoog-V>
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*  http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*  Author:          Victor Hogeweij <Hoog-V>
*
*  This file is part of the IWR6xxx_Toolchain project
*/
/*
*  Barebones MSS example: blinks the GPIO_2 user LED and prints "Hello
*  World!" over the MSS Control UART (MSS_UARTA), to prove the image boots
*  and runs on the radar with no debugger.
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
*
*  --- Stripping RTOS piece 1/N: UART_write() -> UART_writePolling() ---
*  --- Stripping RTOS piece 2/N: Task_sleep() -> busy-wait delay() ---
*  --- Stripping RTOS piece 3/N: Task_create()/BIOS_start() removed ---
*  (see git history for the full reasoning behind pieces 1-3, done while
*  this file was still built on top of SYS/BIOS)
*
*  --- Stripping RTOS piece 4/N: sysbios.aer4f dropped entirely ---
*  This is the last RTOS piece: no SYS/BIOS link, no XDC/configuro, no
*  ti/drivers SDK calls -- src/startup_awr6843.asm now provides the vector
*  table and reset trampoline directly, and every register access below is
*  hand-written instead of going through the SDK's SOC/GPIO/UART/Pinmux
*  drivers.
*
*  Why this is expected to work now, when the very first bare-metal attempt
*  on this file wasn't: that attempt was debugged *before* two unrelated bugs
*  were found and fixed -- CMake's default TI link mode silently corrupting
*  the output, and rts.lib/libc.a needing to be linked after (not before) the
*  objects that reference them. Both were blamed at the time on "bare-metal
*  boot doesn't work on this board", which disassembly of the later
*  SYS/BIOS-based reference build has since disproven: that build's own
*  .vecs table points straight at _c_int00 (no custom trampoline of its own),
*  Core_resetC (the one XDC-generated function that looked like it might be
*  doing hidden essential setup) is dead code nothing calls, and _c_int00
*  itself already enables VFP. None of that ever depended on SYS/BIOS being
*  linked in.
*
*  The one genuinely essential thing SOC_init() did that this file must
*  still replicate is bssClockInit() below: bringing the BSS (radar
*  front-end) out of reset/clock-gate/halt and waiting for its APLL to lock,
*  which is the only way VCLK actually reaches 200MHz. Two things SOC_init()
*  also did are deliberately *not* replicated, as a simplification reasonable
*  for this educational blink+UART example:
*    - SOC_mpu_config()'s full 11+-region MPU setup. The reset trampoline
*      (startup_awr6843.asm) simply leaves the MPU disabled instead, which is
*      a functional superset of any individual region's access rights and
*      has no cache/shareability-sensitive code path here to protect against.
*    - ESM_init(): configures/clears the Error Signaling Module's fault
*      interrupts. Nothing in this program depends on ESM fault reporting.
*
*  Precompiled SDK drivers (libsoc/libuart/libgpio/libpinmux) could not be
*  reused for this piece even if wanted: they're hard-linked against
*  libosal's TI-RTOS variant, which calls real SYS/BIOS Hwi_disable()/
*  Hwi_create() directly, and this SDK version ships no non-RTOS OSAL
*  alternative. Hence hand-written registers below instead of just dropping
*  ti/drivers includes.
*/
#include "AWR6843.h"
#include <stdint.h>

/* Pad numbers + function-select values, from the mmWave SDK's
 * pinmux_xwr68xx.h (SOC_XWR68XX_PIN*_PAD**, SOC_XWR68XX_PIN*_PAD**_<signal>)
 * -- reused as plain numbers here since that header is part of the SDK we're
 * no longer linking against. */
#define PAD_UARTA_TX       30U   /* N5/PADBE  -> MSS_UARTA_TX */
#define PAD_UARTA_TX_FUNC   5U
#define PAD_UARTA_RX       29U   /* N4/PADBD  -> MSS_UARTA_RX */
#define PAD_UARTA_RX_FUNC   2U
#define PAD_GPIO_2         25U   /* K13/PADAZ -> GPIO_2 */
#define PAD_GPIO_2_FUNC     1U

/* GPIO_2 == GPIO_CREATE_INDEX(0, 2) in the SDK (gpio_xwr68xx.h): GIO port A,
 * pin 2. */
#define GPIO_LED_PORT 0U
#define GPIO_LED_PIN  2U

/* MSS_UARTA runs off VCLK, brought to 200MHz by bssClockInit() below --
 * matches the SDK's sys_common_xwr68xx_mss.h MSS_SYS_VCLK. */
#define VCLK_HZ      200000000U
#define UART_BAUD    115200U

/* IOMUX unlock/lock sequence -- ti/drivers/pinmux/src/pinmux.c's
 * Pinmux_Unlock()/Pinmux_Lock(), reused verbatim (magic key values are
 * fixed by the hardware, not something to rederive). */
#define IOMUX_KICK0_UNLOCK 0x83E70B13U
#define IOMUX_KICK1_UNLOCK 0x95A4F1E0U

#define PADCFG_FUNC_SEL_MASK    0xFU
#define PADCFG_IE_OVERRIDE_CTRL (1U << 4)
#define PADCFG_OE_OVERRIDE_CTRL (1U << 6)

static void delay(volatile uint32_t count)
{
    while (count) {
        count--;
    }
}

/* Mux `pad` to `func` and leave its input/output buffer enable under
 * hardware (peripheral) control, i.e. the same "RETAIN_HW_CTRL" mode every
 * pin in this example used under the SDK's Pinmux_Set_OverrideCtrl(). Ports
 * Pinmux_Set_FuncSel() + Pinmux_Set_OverrideCtrl() (ti/drivers/pinmux/src/
 * pinmux.c) down to the one field + two bits they actually change together
 * for that specific combination of arguments. Uses the raw .reg member
 * (not the .bit sub-fields) throughout so behavior only depends on the
 * documented bit offsets/masks, not on how the compiler happens to pack a C
 * bitfield. */
static void pinmuxSetFunc(uint32_t pad, uint32_t func)
{
    IOMUX->IOCFGKICK0 = IOMUX_KICK0_UNLOCK;
    IOMUX->IOCFGKICK1 = IOMUX_KICK1_UNLOCK;

    IOMUX->PADxx_CFG_REG[pad].reg =
        (IOMUX->PADxx_CFG_REG[pad].reg &
         ~(PADCFG_FUNC_SEL_MASK | PADCFG_IE_OVERRIDE_CTRL | PADCFG_OE_OVERRIDE_CTRL)) |
        (func & PADCFG_FUNC_SEL_MASK);

    IOMUX->IOCFGKICK1 = 0U;
    IOMUX->IOCFGKICK0 = 0U;
}

/* Bring the BSS (radar front-end) out of reset/clock-gate/halt and wait for
 * its APLL to lock -- ported from SOC_init()/SOC_ungateClock()/
 * SOC_unhaltBSS()/SOC_waitAPLLCalibration() (ti/drivers/soc/src/soc.c,
 * platform/soc_xwr68xx.c). Those four functions together only ever clear
 * BSSCTL's clock-gate/pclock-gate/reset/halt fields to 0, so a single
 * BSSCTL=0 write reaches the same end state as all four combined -- see
 * AWR6843_TOPRCM.h's header comment for the same simplification. */
static void bssClockInit(void)
{
    TOP_RCM->BSSCTL = 0U;
    TOP_RCM->SPARE0 &= 0x0000FFFFU;
    while ((TOP_RCM->SPARE0 & TOPRCM_SPARE0_APLL_CAL_MASK) != TOPRCM_SPARE0_APLL_CAL_DONE)
    {
    }
}

static void gpioLedInit(void)
{
    pinmuxSetFunc(PAD_GPIO_2, PAD_GPIO_2_FUNC);

    /* GIO_init() in the SDK (gpio.c) also zeroes DIR/DOUT on every other
     * port; skipped here since we never touch those pins and their POR
     * reset state is already input/low. */
    GIO->GIOGCR.reg = 1U;                                          /* release GIO module from reset */
    GIO->GIOPORT[GPIO_LED_PORT].GIODIR.reg |= (1U << GPIO_LED_PIN); /* pin -> output */
}

static void gpioLedWrite(uint32_t level)
{
    if (level)
    {
        GIO->GIOPORT[GPIO_LED_PORT].GIODSET.reg = (1U << GPIO_LED_PIN);
    }
    else
    {
        GIO->GIOPORT[GPIO_LED_PORT].GIODCLR.reg = (1U << GPIO_LED_PIN);
    }
}

/* Bring up MSS_UARTA at 115200 8N1, no interrupts -- register sequence
 * ported from UartSci_open() (ti/drivers/uart/src/uartsci.c), keeping only
 * what a single fixed-config polling UART instance needs (drops DMA setup,
 * semaphores, and Hwi/ISR registration -- all scheduler-only concerns that
 * piece 1/N already established this example doesn't need). */
static void uartInit(void)
{
    pinmuxSetFunc(PAD_UARTA_TX, PAD_UARTA_TX_FUNC);
    pinmuxSetFunc(PAD_UARTA_RX, PAD_UARTA_RX_FUNC);

    SCI_A->SCIGCR0 = 0U;
    SCI_A->SCIGCR0 = 1U;                 /* bring SCI out of reset */

    SCI_A->SCIGCR1 = 0U;                 /* sleep state while configuring */
    SCI_A->SCICLEARINT    = 0xFFFFFFFFU;
    SCI_A->SCICLEARINTLVL = 0xFFFFFFFFU;

    /* Rx/Tx enabled, internal clock, asynchronous timing mode; 1 stop bit
     * and no parity are the reset value of their bits already. */
    SCI_A->SCIGCR1 = SCIGCR1_TXENA | SCIGCR1_RXENA | SCIGCR1_CLOCK | SCIGCR1_TIMING_MODE;

    SCI_A->SCIBAUD = VCLK_HZ / (16U * (UART_BAUD + 1U));
    SCI_A->SCICHAR = 7U;                 /* 8 data bits */

    SCI_A->SCIPIO0 = SCIPIO_RX_BIT | SCIPIO_TX_BIT; /* pins under SCI, not GPIO, control */
    SCI_A->SCIPIO1 = 0U;                            /* driven by the SCI module, not sw */
    SCI_A->SCIPIO3 = 0U;
    SCI_A->SCIPIO6 = 0U;                            /* push-pull, no open drain */
    SCI_A->SCIPIO7 = 0U;
    SCI_A->SCIPIO8 = SCIPIO_RX_BIT | SCIPIO_TX_BIT; /* pull up when idle */

    SCI_A->SCIGCR1 |= SCIGCR1_SW_NRESET;            /* start the SCI */
}

static void uartWritePolling(const uint8_t *buffer, uint32_t size)
{
    while (size--)
    {
        while ((SCI_A->SCIFLR & SCIFLR_TXRDY_BIT) == 0U)
        {
        }
        SCI_A->SCITD = *buffer++;
    }
}

static void blinkHello(void)
{
    gpioLedInit();

    /* Diagnostic value: light the LED solid before touching UART. If the
     * UART were ever to hang, this distinguishes "never reached this
     * function" from "stuck in uartInit()". */
    gpioLedWrite(1U);

    uartInit();

    uint8_t message[] = "Hello World!\r\n";
    uint32_t level = 0U;
    while (1)
    {
        level ^= 1U;
        gpioLedWrite(level);
        uartWritePolling(message, sizeof(message));
        delay(4000000U);
    }
}

int main(void)
{
    /* Essential hardware bring-up SOC_init() used to do -- see the file
     * header comment for what's deliberately not replicated (MPU regions,
     * ESM) and why. */
    bssClockInit();

    blinkHello();

    return 0;
}
