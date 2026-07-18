#ifndef AWR6843_TOPRCM_H
#define AWR6843_TOPRCM_H
#include "AWR6843_types.h"

/*
 * Layout mirrors TI's mmwave_sdk reg_toprcm_xwr16xx.h (TOPRCMRegs), which the
 * SDK's own xwr68xx SOC driver reuses verbatim for this chip -- only the
 * fields actually needed here are named; everything else is reserved
 * padding to keep the real offsets (0x008, 0x0EC, 0x114, 0x1C4) correct.
 * DSSCTL/EXTCLKDIV/EXTCLKSRCSEL/EXTCLKCTL/SOFTSYSRST (0x00C-0x01C) confirmed
 * against the TRM's own "5.8 68xx Control Registers" > "MSS_TOPRCM
 * Registers" table (SWRU520E, this chip's own section -- NOT the 14xx/16xx
 * ones, which happen to share the identical layout since it's the same IP
 * block reused across the family, but this chip's own section is what was
 * actually checked before trusting the offsets).
 */
typedef struct
{
    RwReg CLKDIV;         /* Offset = 0x000 */
    RoReg RESERVED0;       /* Offset = 0x004 */
    RwReg BSSCTL;            /* Offset = 0x008 */
    RwReg DSSCTL;             /* Offset = 0x00C */
    RwReg EXTCLKDIV;           /* Offset = 0x010 */
    RwReg EXTCLKSRCSEL;         /* Offset = 0x014 */
    RwReg EXTCLKCTL;              /* Offset = 0x018 */
    RwReg SOFTSYSRST;               /* Offset = 0x01C */
    RoReg RESERVED1[51];              /* Offset = 0x020 .. 0x0EB */
    RwReg SPARE0;                /* Offset = 0x0EC */
    RoReg RESERVED2[9];           /* Offset = 0x0F0 .. 0x113 */
    RoReg EFUSEREGROW10;           /* Offset = 0x114 */
    RoReg RESERVED3[43];            /* Offset = 0x118 .. 0x1C3 */
    RwReg SECURECFGREG1;             /* Offset = 0x1C4 */
} TOPRCM_Type;

/* SOFTSYSRST: write this exact value to trigger a full chip warm reset
 * (equivalent to the physical reset button, per the TRM) -- confirmed via
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes needing a
 * board reset between DSS test iterations (dssLoad/dssStart can't restart
 * an already-running DSS -- see cli_dss_probe.c), motivating a software
 * alternative to the physical button. */
#define TOPRCM_SOFTSYSRST_TRIGGER_VALUE 0xADU

/* BSSCTL: bits[31:16] halt status/control, [23:16] Pclock gate, [15:8] clock
 * gate, [7:0] reset -- clearing the whole register ungates+deasserts+unhalts
 * the BSS (radar front-end) in one step, matching mmwave_sdk's
 * SOC_ungateClock(SOC_MODULE_BSS) + SOC_unhaltBSS() combined. */
#define TOPRCM_BSSCTL_HALT_STATUS_MASK  0xFFFF0000U

/* SPARE0 bits[17:16] == 3 once BSS has finished APLL calibration and VCLK is
 * actually running at 200 MHz (mmwave_sdk's SOC_waitAPLLCalibration). */
#define TOPRCM_SPARE0_APLL_CAL_MASK     (3U << 16)
#define TOPRCM_SPARE0_APLL_CAL_DONE     (3U << 16)

/* EFUSEREGROW10 bits[25:18]: device part number, used to tell secure and
 * general-purpose (GP) parts apart (mmwave_sdk's SOC_isSecureDevice()). */
#define TOPRCM_EFUSEREGROW10_PART_NUMBER_MASK  (0xFFU << 18)
#define TOPRCM_EFUSEREGROW10_PART_NUMBER_SHIFT 18U
#define TOPRCM_SECURE_PART_NUMBER              0xE3U

/* SECURECFGREG1: on secure parts, each of these 3-bit fields gates a debug
 * peripheral's firewall -- 0 = enabled (blocked), 7 = disabled (accessible).
 * Only the two this project actually uses are named (mmwave_sdk's
 * SOC_controlSecureFirewall() has more: SECURERAM/TRACE/CRYPTO/CUSTCEK in
 * this same register, DMM in SECURECFGREG2). */
#define TOPRCM_SECURECFGREG1_LOGGERFIREWALLEN_MASK (0x7U << 16)
#define TOPRCM_SECURECFGREG1_JTAGFIREWALLEN_MASK   (0x7U << 28)

#endif /* AWR6843_TOPRCM_H */
