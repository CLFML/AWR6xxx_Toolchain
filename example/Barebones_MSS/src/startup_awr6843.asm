;******************************************************************************
; startup_awr6843.asm
;
; Minimal bare-metal boot for the AWR6843 MSS (Cortex-R4F).
;
; Second attempt, now informed by a fully-understood working reference (the
; SYS/BIOS-based example this was progressively stripped down from). What we
; now know for certain, from disassembling that reference, rules out several
; things this file's first version worried about:
;   - The SBL jumps straight at _c_int00 in the working reference -- no
;     custom trampoline, no separate VFP-enable code. _c_int00 itself
;     (standard TI RTS, MRC/MCR on CPACR + VMSR FPEXC) already enables VFP.
;     So this file does NOT need to duplicate that.
;   - ti_sysbios_family_arm_v7r_tms570_Core_resetC (suspected earlier of
;     doing undiscovered essential bring-up) is dead code in the working
;     reference -- nothing calls it. Not the missing piece either.
;   - There is no .pinit/constructor table doing hidden setup before main().
; The one thing that plausibly *does* still matter and was never isolated
; cleanly the first time (because it was tested alongside other, since-fixed
; bugs: missing UART_init()-equivalent setup, a broken CMake link rule) is
; MPU state: TI's own SOC_mpu_config() (ti/drivers/soc, called from
; SOC_init()) explicitly disables the MPU before reconfiguring it with a
; detailed multi-region map. This file takes the simple equivalent -- leave
; the MPU disabled (flat, full-access address space) rather than porting that
; whole region table, since a region-by-region port buys nothing our simple
; blink+UART program needs (no cache/shareability-sensitive access patterns).
;******************************************************************************
        .arm
        .ref  _c_int00
        .def  _resetEntry

;------------------------------------------------------------------------------
; Interrupt vector table -> placed at 0x00000000 by the linker (.intvecs)
;------------------------------------------------------------------------------
        .sect ".intvecs"
        .align 4
        .global _intvecs
_intvecs:
        LDR   pc, reset_addr        ; 0x00 Reset
        LDR   pc, undef_addr        ; 0x04 Undefined instruction
        LDR   pc, svc_addr          ; 0x08 Software interrupt (SVC)
        LDR   pc, pabort_addr       ; 0x0C Prefetch abort
        LDR   pc, dabort_addr       ; 0x10 Data abort
        .word 0                     ; 0x14 Reserved
        LDR   pc, irq_addr          ; 0x18 IRQ
        LDR   pc, fiq_addr          ; 0x1C FIQ

reset_addr   .word _resetEntry
undef_addr   .word _undefEntry
svc_addr     .word _svcEntry
pabort_addr  .word _pabortEntry
dabort_addr  .word _dabortEntry
irq_addr     .word _irqEntry
fiq_addr     .word _fiqEntry

;------------------------------------------------------------------------------
; Reset trampoline: disable the MPU (background/default flat memory map,
; full access everywhere -- see file header), then hand off to the standard
; TI C runtime startup, which sets up the stack, runs cinit, and calls main().
;------------------------------------------------------------------------------
        .text
        .arm
_resetEntry:
        MRC   p15, #0x00, r0, c1, c0, #0x00   ; read SCTLR
        BIC   r0, r0, #0x00000001              ; clear M (MPU enable) bit
        MCR   p15, #0x00, r0, c1, c0, #0x00   ; write SCTLR back
        ISB
        LDR   pc, c_int00_addr
c_int00_addr .word _c_int00

;------------------------------------------------------------------------------
; Minimal exception handlers: spin in place so faults are deterministic
; (a debugger, if attached, will stop here instead of running off into RAM).
;------------------------------------------------------------------------------
_undefEntry:    B  _undefEntry
_svcEntry:      B  _svcEntry
_pabortEntry:   B  _pabortEntry
_dabortEntry:   B  _dabortEntry
_irqEntry:      B  _irqEntry
_fiqEntry:      B  _fiqEntry

        .end
