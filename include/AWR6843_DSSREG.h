#ifndef AWR6843_DSSREG_H
#define AWR6843_DSSREG_H
#include "AWR6843_types.h"

/*
 * DSS_REG: MSS-side control window into the DSP subsystem's power/reset
 * state machine (register block name and field bit positions from the
 * mmWave SDK's reg_dss_xwr68xx.h -- only the two registers this HAL
 * actually uses are modeled, not the full ~2KB block).
 *
 * Per the AWR6843 TRM (swru520e.pdf s5.4.2): on POR the DSP power domain
 * is OFF, and stays that way until explicitly released -- confirmed on
 * real hardware in this project (a raw magic-value write at the very
 * start of DSS's own boot code never appeared in shared memory until
 * this unhalt sequence was added to MSS's own boot). Only GEMPWRSMCFG4
 * (the halt/event-mask control) and GEMPWRSMCFG3 (power-domain status)
 * are needed: the mmWave SDK's own SOC_unhaltDSS() only ever *releases* a
 * halt (GEMPWRSMCFG4.PWRSMLRSTHALT), it never runs the full "power on
 * from cold" sequence documented elsewhere in the TRM -- implying the SBL
 * already powers on DSS and downloads its program (to L2) as part of
 * loading a multicore flash image, but leaves it halted, and it's MSS
 * application software's job to release that halt once ready.
 */
typedef struct {
    RoReg RESERVED[178]; /* Offset = 0x000 - 0x2C4 */
    RwReg GEMPWRSMCFG3;  /* Offset = 0x2C8 */
    RwReg GEMPWRSMCFG4;  /* Offset = 0x2CC */
} DSSREG_Type;

/*! @brief GEMPWRSMCFG3.PWRSMMODESTATUS: DSP power domain state, bits [19:18].
 *         0=OFF, 1=OFF->ON transitioning, 2=ON->OFF transitioning, 3=ON. */
#define DSSREG_GEMPWRSMCFG3_PWRSMMODESTATUS_SHIFT 18U
#define DSSREG_GEMPWRSMCFG3_PWRSMMODESTATUS_MASK  (0x3U << 18U)
#define DSSREG_PWRSMMODESTATUS_ON                 3U

/*! @brief GEMPWRSMCFG4.PWRSMLRSTHALT: 1=DSP execution held at reset after
 *         program download; write 0 to release it and let DSP begin
 *         executing from its reset vector. */
#define DSSREG_GEMPWRSMCFG4_PWRSMLRSTHALT_BIT (1U << 17U)

/*! @brief GEMPWRSMCFG4.GEMEVENTMASK: 1=external DSP wakeup events masked
 *         off (captured but not delivered); clear after unhalting so
 *         normal interrupt routing resumes. */
#define DSSREG_GEMPWRSMCFG4_GEMEVENTMASK_BIT (1U << 18U)

#endif /* AWR6843_DSSREG_H */
