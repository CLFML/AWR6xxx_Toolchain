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
*  This runs on top of a minimal SYS/BIOS (TI-RTOS) rather than being truly
*  bare-metal. That's a deliberate, hard-won choice, not an oversight: no
*  hand-rolled bare-metal boot (custom vector table + reset trampoline) has
*  ever been proven to boot on this AWR6843AOP board. SYS/BIOS's own startup
*  (linked in via sysbios.aer4f) does essential bring-up -- SOC_init() below
*  configures the MPU, among other things -- that a from-scratch reset
*  handler could not practically replicate. See the project's AWR6xxx_Toolchain
*  memory notes for the long debugging history behind this.
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
#include <package/cfg/mss_rtos_per4f.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/esm/esm.h>
#include <ti/drivers/soc/soc.h>
#include <ti/drivers/pinmux/pinmux.h>
#include <ti/drivers/gpio/gpio.h>
#include <ti/drivers/uart/UART.h>
#include <ti/common/sys_common.h>
#include <string.h>

void BlinkHelloTask(UArg arg0, UArg arg1)
{
    UART_Params uartParams;

    /* UART pinmux: N5/PADBE -> MSS_UARTA_TX, N4/PADBD -> MSS_UARTA_RX */
    Pinmux_Set_OverrideCtrl(SOC_XWR68XX_PINN5_PADBE, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR68XX_PINN5_PADBE, SOC_XWR68XX_PINN5_PADBE_MSS_UARTA_TX);
    Pinmux_Set_OverrideCtrl(SOC_XWR68XX_PINN4_PADBD, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR68XX_PINN4_PADBD, SOC_XWR68XX_PINN4_PADBD_MSS_UARTA_RX);

    /* LED pinmux: K13/PADAZ -> GPIO_2 */
    Pinmux_Set_OverrideCtrl(SOC_XWR68XX_PINK13_PADAZ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR68XX_PINK13_PADAZ, SOC_XWR68XX_PINK13_PADAZ_GPIO_2);

    /* Every ti.drivers module needs its own _init() before _open()/other
     * API calls -- easy to miss, and skipping it doesn't error, it just
     * hangs forever with no diagnostic (this exact omission cost an entire
     * debugging session). GPIO_init() specifically releases the GIO module
     * from reset; nothing else in the boot path does that either. */
    GPIO_init();
    GPIO_setConfig(SOC_XWR68XX_GPIO_2, GPIO_CFG_OUTPUT);

    /* Diagnostic value: light the LED solid before touching UART. If
     * UART_open() below were ever to hang again, this distinguishes "never
     * reached this task" from "stuck opening the UART". */
    GPIO_write(SOC_XWR68XX_GPIO_2, 1U);

    UART_init();

    UART_Params_init(&uartParams);
    uartParams.clockFrequency = MSS_SYS_VCLK;
    uartParams.baudRate       = 115200U;
    uartParams.isPinMuxDone   = 1U;

    UART_Handle uartHandle = UART_open(0, &uartParams);
    if (uartHandle == NULL)
    {
        System_printf("Error: BlinkHelloTask unable to open the Command UART Instance\n");
        return;
    }

    uint8_t message[] = "Hello World!\r\n";
    uint32_t level = 0U;
    while (1)
    {
        level ^= 1U;
        GPIO_write(SOC_XWR68XX_GPIO_2, level);
        UART_write(uartHandle, (uint8_t*)message, sizeof(message));
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
