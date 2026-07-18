;-------------------------------------------------------------------------
; startup_dss.asm -- C674x reset entry + interrupt vector table for
; Barebones_DSS, a minimal scheduler-free tick-interrupt diagnostic.
;
; Structurally identical to example/FreeRTOS_DSS's own startup_dss.asm
; (same vector table layout, same RESET->_c_int00 entry), but INT14_VEC
; branches DIRECTLY to a plain cl6x `interrupt`-qualified C function
; (dss_tick_isr, in dss_tick_timer.c) instead of SYS/BIOS's
; Hwi_disp_always.asm dispatcher -- this project deliberately has NO
; FreeRTOS, no task switching, no Hwi_dispatchAlways/Hwi_dispatchC at
; all. An `interrupt`-qualified function's own compiler-generated
; prologue/epilogue does the full register save/restore, so the vector
; needs nothing beyond a plain branch. See dss_freertos_port.md in the
; AWR6xxx_Toolchain memory notes for why this project exists: isolating
; whether the nondeterministic INT14/INT15 dispatch hazard found in
; FreeRTOS_DSS is specific to FreeRTOS's own task-switch/critical-
; section machinery and dispatcher complexity, or a fundamental
; hardware/timing issue present even in the simplest possible case.
;
; Each vector slot MUST be exactly 32 bytes apart -- fixed by the C674x
; hardware itself (RESET at +0x0, NMI +0x20, ..., INT4 +0x80, ..., INT15
; +0x1E0). The explicit `.align 32` after every single vector's code
; (not just once at the top) is load-bearing -- see FreeRTOS_DSS's own
; startup_dss.asm comment for the full story of why (cl6x compresses
; short branches/self-loops to less than 32 bytes otherwise).
;-------------------------------------------------------------------------
    .ref _c_int00
    .ref dss_tick_isr

    .sect ".intvecs"
    .align 32
    .global _resetVectors
_resetVectors:

RESET_VEC:
    MVKL _c_int00, B0
    MVKH _c_int00, B0
    B    B0
    NOP  5
    .align 32

NMI_VEC:
    B    NMI_VEC
    NOP  5
    .align 32

RESERVED2_VEC:
    B    RESERVED2_VEC
    NOP  5
    .align 32

RESERVED3_VEC:
    B    RESERVED3_VEC
    NOP  5
    .align 32

INT4_VEC:
    B    INT4_VEC
    NOP  5
    .align 32

INT5_VEC:
    B    INT5_VEC
    NOP  5
    .align 32

INT6_VEC:
    B    INT6_VEC
    NOP  5
    .align 32

INT7_VEC:
    B    INT7_VEC
    NOP  5
    .align 32

INT8_VEC:
    B    INT8_VEC
    NOP  5
    .align 32

INT9_VEC:
    B    INT9_VEC
    NOP  5
    .align 32

INT10_VEC:
    B    INT10_VEC
    NOP  5
    .align 32

INT11_VEC:
    B    INT11_VEC
    NOP  5
    .align 32

INT12_VEC:
    B    INT12_VEC
    NOP  5
    .align 32

INT13_VEC:
    B    INT13_VEC
    NOP  5
    .align 32

; INT14_VEC -- branches DIRECTLY to the `interrupt`-qualified C ISR, no
; intermediate dispatcher/trampoline of any kind. This is the SIMPLEST
; possible correct vector for this event/vector pairing -- if this
; project still shows the nondeterministic INT14/INT15 hijack, that
; conclusively rules out Hwi_disp_always.asm/Hwi_dispatchC/FreeRTOS's
; task-switch machinery as the cause; if it does NOT, that's equally
; conclusive the other way.
INT14_VEC:
    MVKL dss_tick_isr, B0
    MVKH dss_tick_isr, B0
    B    B0
    NOP  5
    .align 32

INT15_VEC:
    MVKL dss_tick_isr, B0
    MVKH dss_tick_isr, B0
    B    INT15_VEC
    NOP  5
    .align 32
