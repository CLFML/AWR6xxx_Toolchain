#ifndef AWR6843_RTI_H
#define AWR6843_RTI_H
#include "AWR6843_types.h"

/*
 * Register layout ported from TI SYS/BIOS's RTI-based Timer module
 * (ti.sysbios.timers.rti.Timer, bios_6_73_01_01/packages/ti/sysbios/timers/rti/Timer.xdc) --
 * the driver SYS/BIOS itself uses to generate its OS tick on this device
 * family. Base address (SOC_XWR68XX_MSS_RTIB_BASE_ADDRESS, 0xFFFFEE00) is
 * from the mmWave SDK's sys_common_xwr68xx_mss.h, already used elsewhere in
 * this toolchain. Only counter/compare-0 and its interrupt enable/flag
 * registers are named -- this project only uses RTI's free-running
 * counter 0 + Compare 0 for the FreeRTOS tick, the same channel/counter
 * pairing Timer.c's INT0 path uses.
 */
typedef struct
{
    RwReg RTIGCTRL;      /* 0x00: global control (counter 0/1 enable) */
    RoReg RESERVED1[2];   /* 0x04: RTITBCTRL, RTICAPCTRL -- unused */
    RwReg RTICOMPCTRL;     /* 0x0C: selects which free-running counter feeds Compare0/1 */
    RwReg RTIFRC0;           /* 0x10: free-running counter 0 */
    RwReg RTIUC0;             /* 0x14: up counter 0 (prescale counter) */
    RoReg RESERVED2[14];       /* 0x18 .. 0x4C: counter 0/1 capture regs, free-running counter 1 -- unused */
    RwReg RTICOMP0;             /* 0x50: compare 0 value */
    RwReg RTIUDCP0;               /* 0x54: compare 0 auto-reload increment (periodic ticking) */
    RoReg RESERVED3[10];           /* 0x58 .. 0x7C: Compare1-3, timebase compare -- unused */
    RwReg RTISETINTENA;             /* 0x80: interrupt enable set */
    RwReg RTICLEARINTENA;            /* 0x84: interrupt enable clear */
    RwReg RTIINTFLAG;                 /* 0x88: interrupt flag -- write 1 to a bit to clear it */
} RTI_Type;

/* Bit positions confirmed against bios_6_73_01_01's
 * ti/sysbios/timers/rti/Timer.c (its TIMER_GCTRL_..., TIMER_SETINTENA_...,
 * TIMER_INTFLAG_..., and TIMER_COMPCTRL_... defines) and Timer_start()'s
 * actual register poke sequence for the INT0/FRC0 (counter 0) path. */
#define RTIGCTRL_CNT0EN       0x1U
#define RTISETINTENA_INT0     0x1U
#define RTIINTFLAG_INT0       0x1U
/* AND-mask: clears RTICOMPCTRL bit 0, selecting FRC0 as Compare0's source. */
#define RTICOMPCTRL_SEL0_MASK 0xFFFFFFFEU

#endif /* AWR6843_RTI_H */
