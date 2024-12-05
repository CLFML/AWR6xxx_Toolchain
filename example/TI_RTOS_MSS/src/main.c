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
 *  TI RTOS MSS example, prints "Hello World!" over the Control UART
 *  and demonstrates how CMake should be set-up.
 *
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
#include <stdio.h>
#include <string.h>

void BlinkTask(UArg arg0, UArg arg1)
{
    UART_Params         uartParams;
    UART_Handle         uartHandle;
    Pinmux_Set_OverrideCtrl(SOC_XWR68XX_PINN5_PADBE, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR68XX_PINN5_PADBE, SOC_XWR68XX_PINN5_PADBE_MSS_UARTA_TX);
    Pinmux_Set_OverrideCtrl(SOC_XWR68XX_PINN4_PADBD, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR68XX_PINN4_PADBD, SOC_XWR68XX_PINN4_PADBD_MSS_UARTA_RX);

    /*****************************************************************************
     * Open & configure the drivers:
     *****************************************************************************/

    /* Setup the default UART Parameters */
    UART_Params_init(&uartParams);
    uartParams.clockFrequency  = MSS_SYS_VCLK;
    uartParams.baudRate        = 115200U;
    uartParams.isPinMuxDone    = 1U;

    /* Open the UART Instance */
    uartHandle = UART_open(0, &uartParams);
    if (uartHandle == NULL)
    {
        System_printf("Error: Unable to open the Command UART Instance\n");
        return;
    }

    uint8_t message[] = "Hello World!\r\n";
    while (1)
    {
        UART_write(uartHandle, (uint8_t*)message, sizeof(message));
        /* Sleep and poll again: */
        Task_sleep(1000);
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

    /* Initialize the SOC confiugration: */
    memset((void *)&socCfg, 0, sizeof(SOC_Cfg));

    /* Populate the SOC configuration: */
    socCfg.clockCfg = SOC_SysClock_INIT;

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
    Task_create(BlinkTask, &taskParams, NULL);

    BIOS_start();
    return 0;
}
