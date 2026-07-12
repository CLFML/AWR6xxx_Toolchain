#ifndef AWR6843_TOPRCM_H
#define AWR6843_TOPRCM_H
#include "AWR6843_types.h"

/*
 * Layout mirrors TI's mmwave_sdk reg_toprcm_xwr16xx.h (TOPRCMRegs), which the
 * SDK's own xwr68xx SOC driver reuses verbatim for this chip -- only the two
 * fields actually needed here (BSSCTL, SPARE0) are named; everything else is
 * reserved padding to keep the real offsets (0x008, 0x0EC) correct.
 */
typedef struct
{
    RwReg CLKDIV;         /* Offset = 0x000 */
    RoReg RESERVED0;       /* Offset = 0x004 */
    RwReg BSSCTL;            /* Offset = 0x008 */
    RoReg RESERVED1[56];      /* Offset = 0x00C .. 0x0EB */
    RwReg SPARE0;                /* Offset = 0x0EC */
} TOPRCM_Type;

/* BSSCTL: bits[31:16] halt status/control, [23:16] Pclock gate, [15:8] clock
 * gate, [7:0] reset -- clearing the whole register ungates+deasserts+unhalts
 * the BSS (radar front-end) in one step, matching mmwave_sdk's
 * SOC_ungateClock(SOC_MODULE_BSS) + SOC_unhaltBSS() combined. */
#define TOPRCM_BSSCTL_HALT_STATUS_MASK  0xFFFF0000U

/* SPARE0 bits[17:16] == 3 once BSS has finished APLL calibration and VCLK is
 * actually running at 200 MHz (mmwave_sdk's SOC_waitAPLLCalibration). */
#define TOPRCM_SPARE0_APLL_CAL_MASK     (3U << 16)
#define TOPRCM_SPARE0_APLL_CAL_DONE     (3U << 16)

#endif /* AWR6843_TOPRCM_H */
