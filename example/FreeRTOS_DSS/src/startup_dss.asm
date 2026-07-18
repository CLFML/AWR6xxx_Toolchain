;-------------------------------------------------------------------------
; startup_dss.asm -- C674x reset entry + interrupt vector table for
; FreeRTOS_DSS.
;
; Unlike the ARM/Cortex-R4F MSS boot files in this toolchain
; (Barebones_MSS/src/startup_awr6843.asm and friends), this needs no
; custom reset trampoline beyond the vector table itself: C6000's RESET
; vector just branches straight to _c_int00 (the standard TI RTS C/C++
; runtime entry point -- confirmed via nm6x on example/TI_RTOS_DSS's own
; build: _c_int00 sits exactly at that build's ELF entry point, 0x7ec800,
; even under its SYS/BIOS boot chain), which sets up its own initial SP
; from the linker-provided .stack section and calls main() -- no manual
; mode-stack/VFP-enable setup needed the way ARM's exception-mode stacks
; were.
;
; Each vector slot MUST be exactly 32 bytes apart -- that spacing is fixed
; by the C674x hardware itself (RESET at +0x0, NMI +0x20, ..., INT4 +0x80,
; ..., INT15 +0x1E0), not a convention this file gets to choose. The
; explicit `.align 32` after every single vector's code (not just once at
; the top) is load-bearing, not decorative: cl6x's assembler freely
; substitutes compressed 16-bit encodings for short branches/self-loops
; (confirmed via disassembly -- a first version of this file without the
; per-vector .align had INT4_VEC's code land at file offset 0x18 instead
; of hardware's fixed 0x80, since the three preceding "just self-loop"
; vectors compressed down to 4 bytes each instead of staying 32-byte
; slots). Caught before ever touching real hardware by disassembling and
; checking every vector's actual address against its expected hardware
; offset -- see dss_freertos_port.md in the AWR6xxx_Toolchain memory notes.
;
; Only RESET (vector 0) and INT14 (vector 14, dss_tick_timer.c's tick ISR,
; see that file's DSS_TICK_VECTOR_ID) are populated -- INT14 matches
; SYS/BIOS's own reference dss_rtos.cfg default Clock config for this
; exact timer/event, see dss_tick_timer.c's own comment for why. Every
; other vector branches to a self-loop trap -- this port has no other
; interrupt source yet (see portmacro.h's header for why that's the
; current, deliberate scope).
;-------------------------------------------------------------------------
    .ref _c_int00
    .ref ti_sysbios_family_c64p_Hwi_dispatchAlways

; Temporary diagnostic trampoline (ordinary .text, NOT vector-table-
; constrained -- INT14_VEC's own 8-word/32-byte slot is already 100% full,
; no room left to add instrumentation there directly). Stamps a marker at
; a fixed HSRAM cell before falling through into the real, untouched
; Hwi_disp_always dispatcher, so MSS's dssPeek can distinguish "the vector
; never even got this far" from "it entered the dispatcher's own preamble
; and hung somewhere inside that". Deliberately clobbers B0/B1 for its own
; scratch use -- both are registers the dispatcher's own save sequence
; captures a few cycles later, so this corrupts the interrupted context's
; B0/B1 on eventual restore, but this is a throwaway diagnostic build (the
; DSS is expected to need reset+reload after this run regardless) -- see
; dss_freertos_port.md in the AWR6xxx_Toolchain memory notes.
    .global dss_int14_trampoline
DSS_DIAG_TRAMPOLINE_MARKER .set 0x2108004C
DSS_DIAG_TRAMPOLINE_SP     .set 0x21080050
dss_int14_trampoline:
    MVKL DSS_DIAG_TRAMPOLINE_MARKER, B0
    MVKH DSS_DIAG_TRAMPOLINE_MARKER, B0
    MVK  1, B1
    STW  B1, *B0
    MVKL DSS_DIAG_TRAMPOLINE_SP, B0
    MVKH DSS_DIAG_TRAMPOLINE_SP, B0
    STW  B15, *B0
    MVKL ti_sysbios_family_c64p_Hwi_dispatchAlways, B0
    MVKH ti_sysbios_family_c64p_Hwi_dispatchAlways, B0
    B    B0
    NOP  5

; Temporary diagnostic helper, callable as an ordinary C function
; (dss_capture_sp(uint32_t *dst)) -- stamps B15 (SP) as-is (before this
; function's own prologue would otherwise touch it) into *dst, then
; returns via B3 per the standard C6000 calling convention. dst is the
; first argument, in A4 per the standard C6000 calling convention. See
; port_c674.c's Task_enter() for the call site(s) and
; dss_freertos_port.md in the AWR6xxx_Toolchain memory notes.
    .global dss_capture_sp
DSS_DIAG_TASK_ENTER_SP .set 0x21080054
dss_capture_sp:
    STW  B15, *A4
    MVKL DSS_DIAG_TASK_ENTER_SP, B0
    MVKH DSS_DIAG_TASK_ENTER_SP, B0
    STW  B15, *B0
    B    B3
    NOP  5

; Temporary diagnostic: a short, pure NOP delay, callable as an ordinary C
; function (dss_nop_delay(void)) -- tests whether inserting settle cycles
; around portENABLE_INTERRUPTS() changes the observed SP=0 freeze (SPRUGH7
; Section 6.2 documents an MVC-based GIE enable as able to take an
; interrupt on the very next cycle as normal, well-defined behavior, so
; this is not expected to matter per the architecture manual, but it is
; a cheap, safe thing to actually try on real hardware). See
; dss_freertos_port.md in the AWR6xxx_Toolchain memory notes.
    .global dss_nop_delay
dss_nop_delay:
    NOP  8
    NOP  8
    NOP  8
    B    B3
    NOP  5

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

; Temporary diagnostic: instead of a silent self-loop, each of these three
; exception-class vectors stamps its own trap address into a fixed HSRAM
; cell (storing B0's own value at *B0 -- the address doubles as the
; "marker", no separate constant load needed, keeping this within the
; mandatory 32-byte/8-word vector budget) before looping, so MSS's dssPeek
; can tell whether NMIE (IER bit1, see dss_tick_timer.c) is causing an
; actual NMI/exception trap here instead of unblocking the regular INT14
; tick -- see dss_freertos_port.md in the AWR6xxx_Toolchain memory notes.
NMI_VEC:
    MVKL 0x21080040, B0
    MVKH 0x21080040, B0
    B    NMI_VEC
    STW  B0, *B0
    NOP  4
    .align 32

RESERVED2_VEC:
    MVKL 0x21080044, B0
    MVKH 0x21080044, B0
    B    RESERVED2_VEC
    STW  B0, *B0
    NOP  4
    .align 32

RESERVED3_VEC:
    MVKL 0x21080048, B0
    MVKH 0x21080048, B0
    B    RESERVED3_VEC
    STW  B0, *B0
    NOP  4
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

; INT14_VEC now matches SYS/BIOS's own proven vector template EXACTLY
; (byte-for-byte transcribed from example/TI_RTOS_DSS's generated
; configPkg/package/cfg/dss_rtos_pe674.c, ti_sysbios_family_c64p_Hwi_int14's
; block), not just B0 save/restore: it also pushes the vector's own intNum
; (14) onto the stack right below the saved B0, in the SAME slot
; Hwi_disp_always.asm's dispatcher reads back via `ldw *SP[1], a1_intNum`
; once it's built its own aligned stack frame. Without this push, the
; dispatcher would read whatever garbage happened to be on the stack as
; "which interrupt fired" -- this port's earlier direct-to-ISR vectors
; never needed it since they hard-coded the target function themselves.
;
; Branches DIRECTLY into ti_sysbios_family_c64p_Hwi_dispatchAlways now --
; this vector used to hop through dss_int14_trampoline (a diagnostic
; wrapper, ~10 extra instructions) first. TI_RTOS_DSS's own real
; Hwi_int14 vector -- proven reliable on this exact hardware (see
; dss_freertos_port.md's "Attempt 10" in the AWR6xxx_Toolchain memory
; notes) -- branches straight to Hwi_dispatchAlways with no intermediate
; hop at all. Testing whether matching that exactly (removing the extra
; branch/latency the trampoline added right after interrupt entry) fixes
; the nondeterministic hijack into INT15_VEC seen with the trampoline in
; the path -- see "Attempt 11" in that same file for the test result.
INT14_VEC:
    STW  B0, *B15--[2]
    MVK  14, B0
    STW  B0, *B15[1]
    MVKL ti_sysbios_family_c64p_Hwi_dispatchAlways, B0
    MVKH ti_sysbios_family_c64p_Hwi_dispatchAlways, B0
    B    B0
    LDW  *++B15[2], B0
    NOP  4
    .align 32

INT15_VEC:
    B    INT15_VEC
    NOP  5
    .align 32
