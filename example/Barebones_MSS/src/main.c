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
*  This runs on top of a minimal SYS/BIOS (TI-RTOS). That's a deliberate
*  choice for now, not an oversight: a bare-metal (no RTOS) rewrite of this
*  file was attempted at length and reverted -- see git history -- after an
*  unexplained data abort on the very first MSS peripheral register access
*  after boot couldn't be root-caused. SYS/BIOS's own startup does
*  essential bring-up (SOC_init() below configures the MPU, among other
*  things) that a from-scratch reset handler could not practically
*  replicate blind.
*
*  GPIO/pinmux/UART now go through Universal_hal (vendored in
*  Universal_hal/, see that directory's own git remote) instead of the
*  mmWave SDK's precompiled ti/drivers/gpio, ti/drivers/pinmux,
*  ti/drivers/uart -- i.e. Universal_hal's own hand-written register access
*  instead of TI's. SOC_init()/ESM_init() (clock bring-up, MPU, secure
*  firewall) stay on the SDK's ti/drivers/soc and ti/drivers/esm: those
*  aren't GPIO/UART, and Universal_hal has no equivalent for them.
*  Universal_hal's UART support for this chip didn't exist yet -- added
*  here (hal/platform/ti/iwr68xx/uart/) as part of this change, ported from
*  the same mmWave SDK register sequence (ti/drivers/uart/src/uartsci.c)
*  the precompiled driver it replaces was built from.
*
*  UART pin mapping, cross-checked against the AWR6843AOPEVM schematic
*  (PROC091G), the mmWave SDK's pinmux_xwr68xx.h, and a firmware project
*  independently confirmed booting on this exact board:
*     device pad "N5/PADBE" (== ball U16 on the AOP package) -> MSS_UARTA_TX
*     device pad "N4/PADBD" (== ball V16 on the AOP package) -> MSS_UARTA_RX
*  These pads are wired on the EVM to the CP2105 USB-UART bridge (the same
*  bridge UniFlash uses to flash this image), so the output shows up on the
*  "Application/User UART" COM port at 115200 8N1. Universal_hal's own pin
*  table (pinmux_platform_specific.h) had the TX function value for U16
*  wrong (claimed func=2; TI's own pinmux_xwr68xx.h has no func=2 entry for
*  this pad at all, and only func=5 actually produces MSS_UARTA TX output
*  on real hardware) -- fixed as part of this change.
*
*  The GPIO_2 LED pin (K13/PADAZ, == ball K3 on the AOP package) is the same
*  pin the mmWave out-of-box demo uses for its sensor-status LED.
*/
#include <package/cfg/mss_rtos_per4f.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/esm/esm.h>
#include <ti/drivers/soc/soc.h>
#include <ti/common/sys_common.h>
#include <hal_pinmux.h>
#include <hal_gpio.h>
#include <hal_uart.h>
#include <string.h>

void BlinkHelloTask(UArg arg0, UArg arg1)
{
    /* UART pinmux: N5/PADBE (U16) -> MSS_UARTA_TX, N4/PADBD (V16) -> MSS_UARTA_RX */
    pinmux_set_pin_function(MUX_PIN_U16, MUX_FUNC_PIN_U16_MSS_UART_TX);
    pinmux_set_pin_function(MUX_PIN_V16, MUX_FUNC_PIN_V16_MSS_UART_RX);

    /* LED pinmux: K13/PADAZ (K3) -> GPIO_2 */
    pinmux_set_pin_function(MUX_PIN_K3, MUX_FUNC_PIN_K3_GPIO2);

    gpio_set_pin_mode(GPIO_PIN_2, GPIO_MODE_OUTPUT);

    /* Diagnostic value: light the LED solid before touching UART. If
     * uhal_uart_init()/transmit() below were ever to hang, this distinguishes
     * "never reached this task" from "stuck in UART". */
    gpio_set_pin_lvl(GPIO_PIN_2, GPIO_HIGH);

    if (uhal_uart_init(UART_PERIPHERAL_MSS_SCIA, 115200U, UART_CLK_SOURCE_USE_DEFAULT,
                        MSS_SYS_VCLK, UART_EXTRA_OPT_USE_DEFAULT) != UHAL_STATUS_OK)
    {
        System_printf("Error: BlinkHelloTask unable to initialize the Command UART Instance\n");
        return;
    }

    uint8_t message[] = "Hello World!\r\n";
    gpio_level_t level = GPIO_LOW;
    while (1)
    {
        level = (level == GPIO_LOW) ? GPIO_HIGH : GPIO_LOW;
        gpio_set_pin_lvl(GPIO_PIN_2, level);
        uhal_uart_transmit(UART_PERIPHERAL_MSS_SCIA, message, sizeof(message));
        Task_sleep(500);
    }
}

int main(void)
{
    Task_Params taskParams;
    int32_t errCode;
    SOC_Cfg socCfg;
    SOC_Handle socHandle;

    /* Initialize the ESM: */
    ESM_init(0U); // dont clear errors as TI RTOS does it

    /* Initialize the SOC configuration: */
    memset((void *)&socCfg, 0, sizeof(SOC_Cfg));

    /* Populate the SOC configuration: */
    socCfg.clockCfg = SOC_SysClock_INIT;

    /* Initialize the SOC Module: done as soon as the application starts to
     * ensure the MPU is correctly configured, and to bring VCLK up to
     * 200 MHz (which requires un-halting the BSS and waiting for its APLL
     * calibration -- nothing else in the boot path does this either). */
    socHandle = SOC_init(&socCfg, &errCode);
    if (socHandle == NULL)
    {
        System_printf("Error: SOC Module Initialization failed [Error code %d]\n", errCode);
        return -1;
    }

    /* Check if the SOC is a secure device */
    if (SOC_isSecureDevice(socHandle, &errCode))
    {
        /* Disable firewall for JTAG and LOGGER (UART) which is needed by the demo */
        SOC_controlSecureFirewall(socHandle,
                                  (uint32_t)(SOC_SECURE_FIREWALL_JTAG | SOC_SECURE_FIREWALL_LOGGER),
                                  SOC_SECURE_FIREWALL_DISABLE,
                                  &errCode);
    }

    /* Initialize the Task Parameters. */
    Task_Params_init(&taskParams);
    taskParams.priority = 3;
    taskParams.stackSize = 2 * 1024;
    Task_create(BlinkHelloTask, &taskParams, NULL);

    BIOS_start();
    return 0;
}
