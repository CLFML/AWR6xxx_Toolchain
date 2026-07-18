/*
 * hwi_dispatch.c -- statically-populated ti_sysbios_family_c64p_Hwi
 * Module_State plus a minimal Hwi_dispatchC, letting this port reuse
 * SYS/BIOS's own proven Hwi_disp_always.asm dispatcher verbatim (copied
 * unmodified from ~/ti/bios_6_73_01_01/packages/ti/sysbios/family/c64p/
 * Hwi_disp_always.s64P) instead of this port's earlier from-scratch
 * interrupt-qualified-function vector target. That mechanism is already
 * proven working on this exact chip: example/TI_RTOS_DSS's mailbox
 * interrupts go through this identical dispatcher.
 *
 * Module_State's field values follow TI's own PDK FreeRTOS C66x port's
 * static initializer for this exact struct (ti/kernel/freertos/portable/
 * TI_CGT/c66/port_Hwi_c66.c) where applicable; this port has no dispatch
 * table/Hwi_Object abstraction (see ti_sysbios_family_c64p_Hwi_dispatchC__I
 * below), so ierMask/taskSP/isrStack/scw -- fields that PDK's port only
 * needs for the full table-driven dispatch/nesting machinery this project
 * deliberately doesn't replicate -- are left as harmless zero/NULL.
 */
#include "hwi_module_state.h"
#include <stdint.h>

extern int __TI_STATIC_BASE;
extern unsigned int _resetVectors;

/* dispatchAlways re-inits B14 (STATIC_BASE/DP) from Module_State.bss on
 * every interrupt entry -- see Hwi_disp_always.asm's own `ldw *a9, b14`.
 * This project builds with --mem_model:data=far (no user code relies on
 * B14-relative addressing any more), but the dispatcher does this
 * unconditionally, so it's populated with the real linker-provided
 * STATIC_BASE symbol for correctness rather than left dangling. */
ti_sysbios_family_c64p_Hwi_Module_State ti_sysbios_family_c64p_Hwi_Module__state__V = {
    (uint16_t)0,
    (int32_t)0,
    (char *)0,
    (char *)0,
    (void *)&_resetVectors,
    (void *)&__TI_STATIC_BASE,
    (int32_t)0,
};

extern void dss_tick_isr(void);

/*
 * Minimal stand-in for SYS/BIOS's real Hwi_dispatchC()/Hwi_dispatchCore():
 * no Hwi_Object dispatch table, no nesting/hook/Swi-post machinery -- this
 * port has exactly one interrupt source (dss_tick_timer.c's tick, vector
 * 14, see startup_dss.asm's INT14_VEC). Called by Hwi_disp_always.asm with
 * intNum in A4 (standard C6000 first-arg register) after it has already
 * saved full CPU context; the "_always" dispatcher variant ignores this
 * function's return value (it always restores context and returns via
 * IRP, never performs an immediate stack switch on the way out -- see
 * that file's own header comment), so this can be an ordinary C function
 * with an ordinary (unused) return.
 */
/* Temporary diagnostic: unconditionally stamps whatever intNum the
 * dispatcher actually handed us (plus a fixed marker so a stale/leftover
 * HSRAM value from a previous run -- unrelated to this dispatch mechanism
 * -- can't be confused with a genuine call) before the intNum==14 check,
 * so a hang after this point can be told apart from dispatchC never being
 * reached at all. */
#define DSS_DIAG_DISPATCHC_MARKER_ADDRESS 0x21080038U
#define DSS_DIAG_DISPATCHC_INTNUM_ADDRESS 0x2108003CU

int32_t ti_sysbios_family_c64p_Hwi_dispatchC__I(int32_t intNum) {
    *(volatile uint32_t *)DSS_DIAG_DISPATCHC_MARKER_ADDRESS = 0x600DC0DEU;
    *(volatile uint32_t *)DSS_DIAG_DISPATCHC_INTNUM_ADDRESS = (uint32_t)intNum;
    if (intNum == 14) {
        dss_tick_isr();
    }
    return 0;
}
