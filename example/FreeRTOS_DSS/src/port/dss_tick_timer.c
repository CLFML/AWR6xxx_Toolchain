/*
 * dss_tick_timer.c -- programs the DSS's own local RTI (Real-Time
 * Interrupt) timer peripheral to drive the FreeRTOS tick, and routes its
 * interrupt event to a CPU vector via INTMUX.
 *
 * Register layout, bitfields, and the whole init/start sequence are taken
 * directly from SYS/BIOS's own driver source
 * (~/ti/bios_6_73_01_01/packages/ti/sysbios/timers/rti/Timer.c/.h) -- NOT
 * reused by linking that file in (it's an XDC "module" with config-time
 * hooks this project doesn't have, see dss_freertos_port.md in the
 * AWR6xxx_Toolchain memory notes), but hand-transcribed from its
 * Timer_postInit()/Timer_initDevice()/Timer_start() functions, same
 * register writes in the same order.
 *
 * Hardware parameters (base address, interrupt number, event ID, input
 * clock) are NOT guessed -- extracted from example/TI_RTOS_DSS's own
 * XDCtools-generated configPkg/package/cfg/dss_rtos_pe674.c, which
 * resolved dss_rtos.cfg's default (unconfigured) Clock tick source to
 * exactly this hardware:
 *   ti_sysbios_timers_rti_Timer_Module_State_0_device__A[0] = {
 *       intNum = 0xe, eventId = 0x4b, baseAddr = 0x2020000 }
 *   ti_sysbios_timers_rti_Timer_Module_State_0_intFreqs__A[0] = { 0, 0xbebc200 }  (200,000,000 Hz)
 * i.e. this is a genuinely DSS-local peripheral (not shared with/borrowed
 * from MSS's own RTIA/RTIB, which live at a completely different address)
 * -- "RTI" is just TI's reused timer-IP naming convention, present
 * separately on each subsystem.
 *
 * This driver deliberately only uses the timer's "counter 0"/INT0 half
 * (matching SYS/BIOS's own choice of id=0 for the default Clock tick) --
 * counter 1/INT1 (intNum=0xf, eventId=0x4c, same base address) is left
 * unused, available for a future second timer need.
 */
#include <stdint.h>
#include <c6x.h>
#include <FreeRTOS.h>
#include <task.h>

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

/* ---- INTMUX event->vector routing (same register block EventCombiner
 * uses, 0x01800000-range -- core-intrinsic to the C64x+ megamodule, not
 * chip-specific; ported from Hwi_eventMap()'s own algorithm). Uses vector
 * 14 (INT14) -- the SAME vector SYS/BIOS's own reference dss_rtos.cfg
 * default Clock config uses for this exact timer/event (confirmed via
 * example/TI_RTOS_DSS's generated dss_rtos_pe674.c, see this file's other
 * comments) -- switched from an earlier arbitrary choice of vector 4 as a
 * diagnostic: INTMUX/IER/CSR.GIE/GEMEVENTMASK/ISTP were all independently
 * confirmed correct via readback with vector 4, yet the ISR never fired;
 * matching SYS/BIOS's own proven vector choice exactly isolates whether
 * something is specifically wrong with INT4 on this silicon. */
#define DSS_INTMUX1_ADDRESS 0x01800104U
#define DSS_TICK_VECTOR_ID 14

/* Temporary diagnostics: INTMUX1 (0x01800104) is DSP-local-only, not
 * MSS-bus-visible at all (unlike the RTI timer's own registers) -- stash
 * a readback here (and of IER after enabling the vector) into HSRAM so
 * MSS's dssPeek can see them anyway. See dss_freertos_port.md in the
 * AWR6xxx_Toolchain memory notes. */
#define DSS_DIAG_INTMUX1_READBACK_ADDRESS 0x2108000CU
#define DSS_DIAG_IER_READBACK_ADDRESS     0x21080010U

static void dss_map_event_to_vector(uint32_t vectId, uint32_t eventId) {
    volatile uint32_t *muxReg = (volatile uint32_t *)DSS_INTMUX1_ADDRESS;
    uint32_t muxnum = (vectId - 4U) >> 2U;
    uint32_t bitpos = (vectId % 4U) << 3U;

    muxReg[muxnum] = (muxReg[muxnum] & ~(0x7FU << bitpos)) | (eventId << bitpos);

    *(volatile uint32_t *)DSS_DIAG_INTMUX1_READBACK_ADDRESS = muxReg[muxnum];
}

extern void vPortTimerTickHandler(void);

/* No longer `interrupt`-qualified: this port now reuses SYS/BIOS's actual
 * Hwi_disp_always.asm dispatcher (see hwi_dispatch.c), which does its own
 * full save/restore in hand-tuned VLIW assembly and calls into C only via
 * an ordinary function call (Hwi_dispatchC, in a4/A4) -- dss_tick_isr is
 * just the plain C function that dispatchC calls when intNum==14, not a
 * hardware vector target itself any more (startup_dss.asm's INT14_VEC now
 * branches to the dispatcher, not here directly). */
/* Temporary diagnostic: unconditionally stamps a running hit-count at a
 * fixed HSRAM address every time this ISR is actually entered, regardless
 * of anything else -- lets MSS's dssPeek distinguish "ISR never runs"
 * (this counter stays 0) from "ISR runs but something after it is wrong"
 * (this counter increments but main.c's own task counters still don't).
 * See dss_freertos_port.md in the AWR6xxx_Toolchain memory notes. */
#define DSS_TICK_ISR_HITCOUNT_ADDRESS 0x21080008U

void dss_tick_isr(void) {
    volatile uint32_t *hitcount = (volatile uint32_t *)DSS_TICK_ISR_HITCOUNT_ADDRESS;
    (*hitcount)++;
    dssRti0->RTIINTFLAG = TIMER_INTFLAG_INT0;
    vPortTimerTickHandler();
}

/* RTIUC0 (a free-running prescale up-counter, incrementing every RTICLK
 * cycle) is supposed to wrap back to 0 and increment RTIFRC0 by 1 once it
 * reaches RTICPUC0 -- i.e. FRC0 ticks once per (RTICPUC0+1) RTICLK
 * cycles, and RTICOMP0 is compared against FRC0 to generate the
 * interrupt. RTICPUC0=0 ("no prescale", assumed to mean divide-by-1) is a
 * DEGENERATE case that does NOT work on real hardware: confirmed via
 * dssPeek/raw `peek` on real hardware (see dss_freertos_port.md in the
 * AWR6xxx_Toolchain memory notes) that with RTICPUC0=0, RTIUC0 just
 * free-runs forever (observed at ~278 million after ~1.4s, matching an
 * uninterrupted 200MHz count) while RTIFRC0 stays frozen at 1 -- it never
 * wraps/increments again, so it can never reach RTICOMP0 and the
 * interrupt never fires. Using a real, nonzero prescale (divide by 200,
 * giving FRC0 a 1MHz effective rate) avoids this case entirely, for the
 * exact same total period. */
#define DSS_RTI0_PRESCALE 200U

extern unsigned int _resetVectors;
#define DSS_DIAG_ISTP_READBACK_ADDRESS 0x21080018U

/* TSR.XEN/GEE: a SEPARATE gate from CSR.GIE, completely independent of
 * IER/INTMUX/ISTP (all four of those were confirmed correct via readback
 * with the ISR still never firing). SYS/BIOS's own Exception_Module_startup()
 * (~/ti/bios_6_73_01_01/packages/ti/sysbios/family/c64p/Exception.c) sets
 * `TSR |= (TSRXEN | TSRGEE)` as part of its own boot sequence -- this
 * from-scratch port never called anything equivalent. XEN = "eXternal
 * exception ENable" (bit 0x8), GEE = bit 0x4; SYS/BIOS's own comment frames
 * this as being about NMI/exception delivery specifically, but
 * Hwi_disp_always.s64P's own dispatcher ALSO manipulates TSR_XEN_BIT
 * around every regular interrupt's dispatch, suggesting it gates
 * maskable/external interrupts generally, not just NMI -- worth trying
 * given every other prerequisite is independently confirmed correct. */
#define DSS_TSR_XEN_BIT 0x8U
#define DSS_TSR_GEE_BIT 0x4U
/* TSR also has its OWN "SGIE"(bit1)/"GIE"(bit0) fields -- separate from
 * CSR.GIE -- per Exception_asm.s64P's own NTSR bit-layout diagram, which
 * documents "DBGM, XEN, SGIE and GIE [as] arbitrary values controlled by
 * the application" (i.e. not hardware-fixed, software must set them
 * appropriately). SYS/BIOS's own boot only sets XEN/GEE; testing with
 * SGIE/TSR.GIE also set, since everything else checks out correct via
 * readback yet the interrupt still never gets taken. */
#define DSS_TSR_SGIE_BIT 0x2U
#define DSS_TSR_GIE_BIT  0x1U
#define DSS_DIAG_TSR_READBACK_ADDRESS 0x2108001CU
#define DSS_DIAG_IFR_READBACK_ADDRESS 0x21080020U
#define DSS_DIAG_IFR_TIMEOUT_ADDRESS  0x21080024U

/* TSR.EXC (bit 0x400) -- "exception in progress" latch. SYS/BIOS's own
 * Exception_Module_startup() (~/ti/bios_6_73_01_01/packages/ti/sysbios/
 * family/c64p/Exception.c) does `TSR &= ~(Exception_TSREXC)` right after
 * setting XEN|GEE, with the comment "clear EXC bit in TSR from previous
 * exception processing" -- this port had copied the XEN|GEE half but never
 * the EXC-clear half. Since TI_RTOS_DSS itself runs with NMIE
 * (IER bit 1) set in its own ierMask (0x4783) and is CONFIRMED WORKING on
 * this exact hardware, NMIE is not inherently fatal here -- so a stale
 * TSR.EXC left set (this project already confirmed via main.c's L1P-cache
 * comment that a warm SOFTSYSRST does NOT clear all latched CPU/cache
 * state; a previous NMIE test that froze the core mid-exception-entry is a
 * very plausible way to leave EXC stuck set) is a concrete, testable
 * candidate for why enabling NMIE previously froze the CPU instantly with
 * no vector ever taken. */
#define DSS_TSR_EXC_BIT 0x400U

/* IER bit 1 ("NMIE" by generic C6000 convention) -- REQUIRED, now set
 * below. NMIE gates ALL of INT4-15 delivery on this core (SPRUGH7
 * Section 7.2.1: "When NMIE = 0, all interrupts and external exceptions
 * are disabled"), not just NMI itself -- confirmed empirically too, the
 * tick ISR never fires at all without it.
 *
 * A live JTAG/XDS110 trace (see dss_freertos_port.md's "Attempt 8" in
 * the AWR6xxx_Toolchain memory notes for the full session) finally
 * root-caused what long looked like "SP corrupts to exactly 0x0 inside
 * the dispatcher": it was never SP corruption. Single-stepping this
 * whole path (which is how every earlier investigation observed it)
 * turns out to SUPPRESS interrupt recognition on this core entirely --
 * at full run speed, the tick interrupt dispatches correctly, B15/SP
 * stays sane at every instruction boundary through INT14_VEC's own
 * save/restore AND into dss_int14_trampoline -- and then, with no
 * branch instruction to explain it, a SECOND, nested interrupt/
 * exception hijacks control mid-trampoline and lands in INT15_VEC's
 * dead self-loop trap. Confirmed genuinely nondeterministic: racing
 * hardware breakpoints on both INT14_VEC and INT15_VEC from the same
 * clean reset landed on different vectors across different runs.
 *
 * Prime suspect: TSR.GEE (see DSS_TSR_GEE_BIT below), which puts NMI
 * handling into "exception mode" for functionality (external exceptions,
 * NMI-as-exception) this port never actually uses -- removed below as
 * the first concrete test of that theory. TSR.EXC is still kept clear
 * (a separate, genuinely required fix -- see DSS_TSR_EXC_BIT). */
#define DSS_IER_NMIE_BIT 0x2U

void dss_tick_timer_start(void) {
    uint32_t prescaledClockHz;
    uint32_t period;

    /* GEE deliberately NOT set here (unlike SYS/BIOS's own
     * Exception_Module_startup(), which sets XEN|GEE together) -- a live
     * JTAG trace root-caused a nondeterministic second/nested
     * interrupt-vs-exception dispatch hijacking control mid-ISR (landing
     * in INT15_VEC's dead trap) to TSR.GEE putting NMI handling into
     * "exception mode" for functionality (NMI-as-exception, external
     * EXCEP) this port never uses. XEN alone (without GEE) should be
     * inert -- exception recognition requires GEE first per the CPU
     * reference -- so it's left set to stay close to the proven SYS/BIOS
     * sequence otherwise. See DSS_IER_NMIE_BIT's comment and
     * dss_freertos_port.md's "Attempt 8"/"Attempt 9" in the
     * AWR6xxx_Toolchain memory notes for the full trace that found this
     * and this fix's hardware test result. */
    TSR |= (DSS_TSR_XEN_BIT);
    TSR &= ~(DSS_TSR_EXC_BIT | DSS_TSR_GEE_BIT);
    *(volatile uint32_t *)DSS_DIAG_TSR_READBACK_ADDRESS = TSR;

    *(volatile uint32_t *)DSS_TICK_ISR_HITCOUNT_ADDRESS = 0U;

    /* ISTP (Interrupt Service Table Pointer, a CPU control register) must
     * explicitly point at the vector table's base for INT4-15/NMI to
     * vector correctly -- unlike RESET (hardwired to a fixed, separately
     * remapped address, already confirmed working since boot reaches this
     * function at all), the CPU has no reason to assume ISTP matches that
     * same address on its own. SYS/BIOS's own boot sequence
     * (Hwi_Module_startup(), deliberately not reused here -- see this
     * file's own top-of-file comment) sets this implicitly; a from-scratch
     * startup must do it explicitly. `_resetVectors` is startup_dss.asm's
     * own vector table base symbol. */
    ISTP = (uint32_t)&_resetVectors;
    *(volatile uint32_t *)DSS_DIAG_ISTP_READBACK_ADDRESS = ISTP;

    dss_map_event_to_vector(DSS_TICK_VECTOR_ID, DSS_RTI0_INT0_EVENT_ID);

    /* ICR = 0xffff: clears the CPU's OWN interrupt-controller pending-flag
     * latch (IFR) -- a SEPARATE thing from the RTI peripheral's own
     * RTIINTFLAG, and from IER (which only masks, doesn't clear). SYS/BIOS's
     * Hwi_Module_startup() does this right after INTMUX setup, "to start
     * with a clean slate" -- skipped here originally. */
    ICR = 0xFFFFU;

    /* Timer_initDevice(): stop, clear compare/prescale/flags. */
    dssRti0->RTIGCTRL &= ~TIMER_GCTRL_CNT0EN;
    dssRti0->RTIUDCP0 = 0;
    dssRti0->RTICOMP0 = 0;
    dssRti0->RTICPUC0 = 0;

    /* Timer_postInit(): internal up-counter, prescale by
     * DSS_RTI0_PRESCALE (see this function's header comment for why 0
     * doesn't work), enable INT0, select counter 0 for compare. */
    dssRti0->RTITBCTRL &= TIMER_TBCTRL_TBEXT;
    dssRti0->RTICPUC0 = DSS_RTI0_PRESCALE - 1U;
    dssRti0->RTISETINTENA |= TIMER_SETINTENA_INT0;
    dssRti0->RTICOMPCTRL &= TIMER_COMPCTRL_SEL0;

    /* Timer_start(): load period, clear pending flag, enable CPU-side
     * vector, start counting.
     *
     * RTIUDCP0 ("update compare period 0") is a SECOND real bug this
     * function had: on a compare match, the hardware auto-adds UDCP0 to
     * COMP0 to arm the next match, rather than resetting FRC0 -- it does
     * NOT wrap/reset FRC0 itself (confirmed via real hardware: FRC0 kept
     * counting past COMP0, past 25 million, with RTIINTFLAG's INT0 bit
     * stuck set the whole time -- one single match, never rearmed).
     * SYS/BIOS's own Timer_setPeriod() sets UDCP0 to the same value as
     * COMP0 (see ~/ti/bios_6_73_01_01/packages/ti/sysbios/timers/rti/
     * Timer.c) -- missed on the first pass here since it's set in a
     * DIFFERENT function (Timer_setPeriod(), called from
     * Timer_postInit()'s Timer_setPeriodMicroSecs() path) than the one
     * that sets the INITIAL COMP0 value (Timer_start()). Without it, the
     * timer interrupts exactly once, ever. */
    prescaledClockHz = DSS_RTI0_INPUT_CLOCK_HZ / DSS_RTI0_PRESCALE;
    period = (prescaledClockHz / configTICK_RATE_HZ) - 1U;
    dssRti0->RTIUC0 = 0;
    dssRti0->RTIFRC0 = 0;
    dssRti0->RTICOMP0 = period;
    dssRti0->RTIUDCP0 = period;
    dssRti0->RTIINTFLAG = TIMER_INTFLAG_INT0;

    IER |= (1U << DSS_TICK_VECTOR_ID) | DSS_IER_NMIE_BIT;
    *(volatile uint32_t *)DSS_DIAG_IER_READBACK_ADDRESS = IER;

    dssRti0->RTIGCTRL |= TIMER_GCTRL_CNT0EN;

    /* Temporary diagnostic: still with global interrupts disabled (this
     * whole function runs before Task_enter() ever re-enables them), busy-
     * wait for the PERIPHERAL's own RTIINTFLAG to show a match (confirmed
     * elsewhere this happens quickly, ~1ms), then immediately read back
     * IFR -- the CPU's OWN interrupt-controller pending-flag register,
     * separate from RTIINTFLAG. If IFR's bit for this vector is set here,
     * the edge WAS captured by the CPU's controller (meaning something
     * else entirely blocks it from being taken); if clear, the CPU's own
     * controller never saw it at all (pointing back at INTMUX/routing
     * despite the earlier INTMUX1 readback looking correct). */
    {
        uint32_t timeout = 10000000U;
        while (((dssRti0->RTIINTFLAG & TIMER_INTFLAG_INT0) == 0U) && (timeout != 0U)) {
            timeout--;
        }
        *(volatile uint32_t *)DSS_DIAG_IFR_READBACK_ADDRESS = IFR;
        *(volatile uint32_t *)DSS_DIAG_IFR_TIMEOUT_ADDRESS = timeout;
    }
}
