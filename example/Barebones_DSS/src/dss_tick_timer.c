/*
 * dss_tick_timer.c -- programs the DSS's own local RTI (Real-Time
 * Interrupt) timer peripheral and routes its interrupt event to CPU
 * vector 14 via INTMUX. Adapted from example/FreeRTOS_DSS's own
 * src/port/dss_tick_timer.c (byte-for-byte identical register
 * sequence/values, all independently proven correct there) -- the only
 * change is dss_tick_isr() itself: a plain cl6x `interrupt`-qualified C
 * function instead of a call into SYS/BIOS's Hwi_disp_always.asm
 * dispatcher, and no FreeRTOS.h/task.h dependency (configTICK_RATE_HZ
 * hardcoded below instead). See dss_freertos_port.md in the
 * AWR6xxx_Toolchain memory notes for the full hardware-parameter
 * provenance (register layout/values transcribed from SYS/BIOS's own
 * ~/ti/bios_6_73_01_01/packages/ti/sysbios/timers/rti/Timer.c, hardware
 * parameters extracted from example/TI_RTOS_DSS's generated
 * dss_rtos_pe674.c) and this project's own reason for existing:
 * isolating whether the nondeterministic INT14/INT15 dispatch hazard is
 * FreeRTOS-specific or a fundamental hardware/timing issue.
 */
#include <stdint.h>
#include <c6x.h>

#define DSS_TICK_RATE_HZ 1000U

/* ---- RTI device registers (Timer.h's DeviceRegs, field-for-field) ---- */
typedef struct {
    uint32_t RTIGCTRL;
    uint32_t RTITBCTRL;
    uint32_t RTICAPCTRL;
    uint32_t RTICOMPCTRL;
    uint32_t RTIFRC0;
    uint32_t RTIUC0;
    uint32_t RTICPUC0;
    uint32_t RESERVED0;
    uint32_t RTICAFRC0;
    uint32_t RTICAUC0;
    uint32_t RESERVED1;
    uint32_t RESERVED2;
    uint32_t RTIFRC1;
    uint32_t RTIUC1;
    uint32_t RTICPUC1;
    uint32_t RESERVED3;
    uint32_t RTICAFRC1;
    uint32_t RTICAUC1;
    uint32_t RESERVED4;
    uint32_t RESERVED5;
    uint32_t RTICOMP0;
    uint32_t RTIUDCP0;
    uint32_t RTICOMP1;
    uint32_t RTIUDCP1;
    uint32_t RTICOMP2;
    uint32_t RTIUDCP2;
    uint32_t RTICOMP3;
    uint32_t RTIUDCP3;
    uint32_t RTITBLCOMP;
    uint32_t RTITBLHCOMP;
    uint32_t RESERVED6;
    uint32_t RESERVED7;
    uint32_t RTISETINTENA;
    uint32_t RTICLEARINTENA;
    uint32_t RTIINTFLAG;
} dss_rti_regs_t;

#define DSS_RTI0_BASE_ADDRESS 0x02020000U
#define DSS_RTI0_INT0_EVENT_ID 0x4BU
#define DSS_RTI0_INPUT_CLOCK_HZ 200000000U

#define TIMER_GCTRL_CNT0EN   0x1U
#define TIMER_TBCTRL_TBEXT   0xFFFFFFFEU
#define TIMER_COMPCTRL_SEL0  0xFFFFFFFEU
#define TIMER_SETINTENA_INT0 0x1U
#define TIMER_INTFLAG_INT0   0x1U

static dss_rti_regs_t *const dssRti0 = (dss_rti_regs_t *)DSS_RTI0_BASE_ADDRESS;

#define DSS_INTMUX1_ADDRESS 0x01800104U
#define DSS_TICK_VECTOR_ID 14

#define DSS_DIAG_INTMUX1_READBACK_ADDRESS 0x2108000CU
#define DSS_DIAG_IER_READBACK_ADDRESS     0x21080010U

static void dss_map_event_to_vector(uint32_t vectId, uint32_t eventId) {
    volatile uint32_t *muxReg = (volatile uint32_t *)DSS_INTMUX1_ADDRESS;
    uint32_t muxnum = (vectId - 4U) >> 2U;
    uint32_t bitpos = (vectId % 4U) << 3U;

    muxReg[muxnum] = (muxReg[muxnum] & ~(0x7FU << bitpos)) | (eventId << bitpos);

    *(volatile uint32_t *)DSS_DIAG_INTMUX1_READBACK_ADDRESS = muxReg[muxnum];
}

/* Diagnostic HSRAM addresses -- same layout as FreeRTOS_DSS's own, so
 * the same test_gee_fix.py-style dssPeek polling script works unchanged
 * against this project too. */
#define DSS_TICK_ISR_HITCOUNT_ADDRESS 0x21080008U
#define DSS_IDLE_HITCOUNT_ADDRESS     0x21080028U

/* Plain `interrupt`-qualified C ISR -- cl6x generates the full register
 * save/restore prologue/epilogue automatically, so INT14_VEC
 * (startup_dss.asm) can branch straight here with nothing in between.
 * This is the simplest possible correct interrupt handler on this
 * core -- no Hwi_disp_always.asm, no Hwi_dispatchC, no FreeRTOS task
 * switch of any kind. */
interrupt void dss_tick_isr(void) {
    volatile uint32_t *hitcount = (volatile uint32_t *)DSS_TICK_ISR_HITCOUNT_ADDRESS;
    (*hitcount)++;
    dssRti0->RTIINTFLAG = TIMER_INTFLAG_INT0;
}

#define DSS_RTI0_PRESCALE 200U

extern unsigned int _resetVectors;
#define DSS_DIAG_ISTP_READBACK_ADDRESS 0x21080018U

#define DSS_TSR_XEN_BIT 0x8U
#define DSS_TSR_GEE_BIT 0x4U
#define DSS_DIAG_TSR_READBACK_ADDRESS 0x2108001CU
#define DSS_DIAG_IFR_READBACK_ADDRESS 0x21080020U
#define DSS_DIAG_IFR_TIMEOUT_ADDRESS  0x21080024U

#define DSS_TSR_EXC_BIT 0x400U

#define DSS_IER_NMIE_BIT 0x2U

void dss_tick_timer_start(void) {
    uint32_t prescaledClockHz;
    uint32_t period;

    /* GEE deliberately NOT set -- see example/FreeRTOS_DSS's
     * dss_tick_timer.c for the full story (Attempt 9 in
     * dss_freertos_port.md found this ruled out as the sole cause, but
     * kept anyway since it matches the minimal set of TSR bits this
     * project actually needs). TSR.EXC stays cleared (a genuinely
     * required fix, Attempt 1 in that same file). */
    TSR |= (DSS_TSR_XEN_BIT);
    TSR &= ~(DSS_TSR_EXC_BIT | DSS_TSR_GEE_BIT);
    *(volatile uint32_t *)DSS_DIAG_TSR_READBACK_ADDRESS = TSR;

    *(volatile uint32_t *)DSS_TICK_ISR_HITCOUNT_ADDRESS = 0U;
    *(volatile uint32_t *)DSS_IDLE_HITCOUNT_ADDRESS = 0U;

    ISTP = (uint32_t)&_resetVectors;
    *(volatile uint32_t *)DSS_DIAG_ISTP_READBACK_ADDRESS = ISTP;

    dss_map_event_to_vector(DSS_TICK_VECTOR_ID, DSS_RTI0_INT0_EVENT_ID);

    ICR = 0xFFFFU;

    /* Timer_initDevice(): stop, clear compare/prescale/flags. */
    dssRti0->RTIGCTRL &= ~TIMER_GCTRL_CNT0EN;
    dssRti0->RTIUDCP0 = 0;
    dssRti0->RTICOMP0 = 0;
    dssRti0->RTICPUC0 = 0;

    /* Timer_postInit(): internal up-counter, prescale by
     * DSS_RTI0_PRESCALE, enable INT0, select counter 0 for compare. */
    dssRti0->RTITBCTRL &= TIMER_TBCTRL_TBEXT;
    dssRti0->RTICPUC0 = DSS_RTI0_PRESCALE - 1U;
    dssRti0->RTISETINTENA |= TIMER_SETINTENA_INT0;
    dssRti0->RTICOMPCTRL &= TIMER_COMPCTRL_SEL0;

    /* Timer_start(): load period, clear pending flag, enable CPU-side
     * vector, start counting. RTIUDCP0 must equal the period for
     * continuous re-arming -- see FreeRTOS_DSS's own comment for the
     * hardware bug this avoids (without it, the timer interrupts
     * exactly once, ever). */
    prescaledClockHz = DSS_RTI0_INPUT_CLOCK_HZ / DSS_RTI0_PRESCALE;
    period = (prescaledClockHz / DSS_TICK_RATE_HZ) - 1U;
    dssRti0->RTIUC0 = 0;
    dssRti0->RTIFRC0 = 0;
    dssRti0->RTICOMP0 = period;
    dssRti0->RTIUDCP0 = period;
    dssRti0->RTIINTFLAG = TIMER_INTFLAG_INT0;

    IER |= (1U << DSS_TICK_VECTOR_ID) | DSS_IER_NMIE_BIT;
    *(volatile uint32_t *)DSS_DIAG_IER_READBACK_ADDRESS = IER;

    dssRti0->RTIGCTRL |= TIMER_GCTRL_CNT0EN;

    {
        uint32_t timeout = 10000000U;
        while (((dssRti0->RTIINTFLAG & TIMER_INTFLAG_INT0) == 0U) && (timeout != 0U)) {
            timeout--;
        }
        *(volatile uint32_t *)DSS_DIAG_IFR_READBACK_ADDRESS = IFR;
        *(volatile uint32_t *)DSS_DIAG_IFR_TIMEOUT_ADDRESS = timeout;
    }
}
