/*
 * main.c -- Barebones_DSS: minimal, scheduler-free tick-interrupt
 * diagnostic for the AWR6843 DSS (C674x/C64x+). No FreeRTOS, no task
 * switching, no SYS/BIOS -- just: set up the RTI0 tick timer, enable
 * interrupts, spin forever counting idle iterations while the ISR
 * (dss_tick_timer.c's dss_tick_isr) counts ticks. See
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes for why
 * this project exists: example/FreeRTOS_DSS's own tick ISR shows a
 * nondeterministic hijack into INT15_VEC's dead trap at the exact
 * moment interrupts are first enabled, even after two real, confirmed
 * fixes eliminated the total-freeze failure mode. This project tests
 * the SAME timer/vector/event mechanism with everything else (task
 * switching, critical sections, SYS/BIOS's dispatcher) stripped away,
 * to isolate whether the hazard is FreeRTOS-specific or fundamental.
 */
#include <stdint.h>
#include <c6x.h>

extern void dss_tick_timer_start(void);

#define DSS_IDLE_HITCOUNT_ADDRESS 0x21080028U
#define DSS_TICK_VECTOR_ID 14U

int main(void) {
    volatile uint32_t *idleHitcount = (volatile uint32_t *)DSS_IDLE_HITCOUNT_ADDRESS;

    _disable_interrupts();

    dss_tick_timer_start();

    /* Clear any stale pending flag immediately before enabling GIE --
     * matches example/FreeRTOS_DSS's own Attempt 12/13 fix (see
     * dss_freertos_port.md). Nothing else runs between the timer setup
     * above and this enable -- the simplest possible version of that
     * same ordering. */
    ICR = (1U << DSS_TICK_VECTOR_ID);
    _enable_interrupts();

    for (;;) {
        (*idleHitcount)++;
    }
}
