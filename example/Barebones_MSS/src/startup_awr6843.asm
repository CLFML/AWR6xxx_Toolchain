;******************************************************************************
; startup_awr6843.asm
;
; Minimal bare-metal boot for the AWR6843 MSS (Cortex-R4F).
;
; Third attempt. The first two (see this project's own git history and the
; AWR6xxx_Toolchain memory notes) both jumped straight into the standard TI
; RTS's _c_int00 after minimal setup, and both hit a reliable data abort on
; the first MSS peripheral register access after boot -- never root-caused,
; eventually abandoned in favor of SYS/BIOS.
;
; This attempt is informed by actually reading what SYS/BIOS's own generated
; startup does differently (see the AWR6xxx_Toolchain memory note on
; xdctools/configuro's generated output) that a plain RTS _c_int00 does not:
;   - ti.sysbios.family.arm.exc.Exception's Module_startup calls
;     Exception_initCore0() (ti/sysbios/family/arm/exc/Exception_asm.asm),
;     which explicitly switches to Abort mode and to Undefined mode and
;     initializes EACH mode's own banked stack pointer (SP_abt, SP_und).
;     A plain RTS _c_int00 only ever sets up the SVC/System-mode stack --
;     Abort/Undefined mode banked SPs are left at their reset value
;     (undefined/garbage) unless something explicitly sets them. This is the
;     leading suspect for the "first peripheral access data-aborts" symptom:
;     if SP_abt is garbage, the very first Data Abort exception's own
;     prologue (pushing return state) corrupts memory at a garbage address
;     instead of cleanly entering the abort handler.
;   - ti.sysbios.family.arm.v7r.vim.Hwi's Module_startup does a VIM hardware
;     soft-reset (SOFTRST2 register) and, gated by an errata flag for this
;     part, an unconditional ESM clear (ESMSR1-4/ESMSSR2 <- 0xFFFFFFFF).
; This file explicitly replicates both of those steps, plus sets up IRQ/FIQ
; mode stacks too (cheap insurance, even though interrupts are never enabled
; in this minimal example -- Universal_hal's esm_init() needs SYS/BIOS's
; Hwi_create() and isn't used here). Confirmed booting on real hardware.
;
; MPU: earlier attempts left the MPU disabled (flat, full-access) rather
; than porting TI's SOC_mpu_config() region table. That table has since been
; fully ported (Universal_hal's soc_init()/mpu_config(), see
; AWR6xxx_Toolchain memory notes) and IS used here, from main() -- this file
; only disables the MPU early (its boot-time state is otherwise unknown) so
; soc_init() can configure it cleanly from a known state.
;******************************************************************************
        .arm
        .ref  __TI_auto_init
        .ref  main
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
; Per-mode exception stacks. One shared "exception stack" for Abort and
; Undefined mode (they can't both be active at once -- matches
; Exception_initCore0's own convention of pointing SP_abt and SP_und at the
; same region), plus separate IRQ/FIQ stacks. 512 bytes each is generous for
; this program (nothing here ever runs deeply nested, and no interrupt is
; ever enabled), 8-byte aligned per EABI.
;------------------------------------------------------------------------------
        .bss  _excStack, 512, 8
        .bss  _irqStack, 512, 8
        .bss  _fiqStack, 512, 8

;------------------------------------------------------------------------------
; Reset trampoline.
;------------------------------------------------------------------------------
        .text
        .arm
_resetEntry:
        ;*------------------------------------------------------
        ;* Disable the MPU -- unknown state at boot; soc_init()
        ;* (called from main(), see file header) configures it
        ;* properly from this known-disabled starting point.
        ;*------------------------------------------------------
        MRC   p15, #0x00, r0, c1, c0, #0x00   ; read SCTLR
        BIC   r0, r0, #0x00000001              ; clear M (MPU enable) bit
        MCR   p15, #0x00, r0, c1, c0, #0x00   ; write SCTLR back
        ISB

        ;*------------------------------------------------------
        ;* Enable the VFP coprocessor: full CP10/CP11 access via
        ;* CPACR, then FPEXC.EN. R4F-toolchain.cmake builds with
        ;* --float_support=VFPv3D16 (hard-float), so any `float`
        ;* arithmetic emits real VFP instructions directly -- the
        ;* first one executed without this traps as an Undefined
        ;* Instruction exception. Confirmed missing (and fixed)
        ;* the same way in every GCC-based example's
        ;* startup_awr6843.S after GCC_FreeRTOS_VitalSigns_MSS hit
        ;* it on real hardware -- this project shares the same gap.
        ;*------------------------------------------------------
        MRC   p15, #0, r0, c1, c0, #2   ; read CPACR
        ORR   r0, r0, #(0xF << 20)      ; full access to CP10/CP11
        MCR   p15, #0, r0, c1, c0, #2   ; write CPACR
        ISB
        MOV   r0, #0x40000000           ; FPEXC.EN
        VMSR  fpexc, r0

        ;*------------------------------------------------------
        ;* Abort mode: set SP_abt (shared exception stack)
        ;*------------------------------------------------------
        MRS   r0, cpsr
        BIC   r0, r0, #0x1F
        ORR   r0, r0, #0x17     ; Abort mode
        MSR   cpsr_cf, r0
        LDR   sp, excstack_top_addr

        ;*------------------------------------------------------
        ;* Undefined mode: set SP_und (same shared exception stack)
        ;*------------------------------------------------------
        MRS   r0, cpsr
        BIC   r0, r0, #0x1F
        ORR   r0, r0, #0x1B     ; Undefined mode
        MSR   cpsr_cf, r0
        LDR   sp, excstack_top_addr

        ;*------------------------------------------------------
        ;* IRQ mode: set SP_irq
        ;*------------------------------------------------------
        MRS   r0, cpsr
        BIC   r0, r0, #0x1F
        ORR   r0, r0, #0x12     ; IRQ mode
        MSR   cpsr_cf, r0
        LDR   sp, irqstack_top_addr

        ;*------------------------------------------------------
        ;* FIQ mode: set SP_fiq
        ;*------------------------------------------------------
        MRS   r0, cpsr
        BIC   r0, r0, #0x1F
        ORR   r0, r0, #0x11     ; FIQ mode
        MSR   cpsr_cf, r0
        LDR   sp, fiqstack_top_addr

        ;*------------------------------------------------------
        ;* Back to System mode for the SVC/System stack (shared
        ;* with User mode banking; this program never enters
        ;* User mode). __stack/__STACK_SIZE come from the RTS
        ;* boot convention (linker-provided, see linker_awr6843.cmd's
        ;* -stack directive and boot.asm's own use of the same symbols).
        ;*------------------------------------------------------
        MRS   r0, cpsr
        BIC   r0, r0, #0x1F
        ORR   r0, r0, #0x1F     ; System mode
        MSR   cpsr_cf, r0
        LDR   r0, stack_base_addr
        LDR   r1, stack_size_addr
        ADD   sp, r0, r1
        BIC   sp, sp, #0x07     ; 64-bit align (EABI)

        ;*------------------------------------------------------
        ;* VIM hardware soft-reset (SOFTRST2, RCM_BASE + 0x08).
        ;* Mirrors ti.sysbios.family.arm.v7r.vim.Hwi's
        ;* Hwi_initIntController(): assert VIM-only reset, then
        ;* de-assert. No interrupts are enabled in this program,
        ;* but this puts the VIM peripheral itself into the same
        ;* known state SYS/BIOS's startup would.
        ;*------------------------------------------------------
        LDR   r0, softrst2_addr
        LDR   r1, [r0]
        LDR   r2, vim_reset_assert_mask
        ORR   r1, r1, r2
        STR   r1, [r0]
        LDR   r2, vim_reset_clear_mask
        BIC   r1, r1, r2
        STR   r1, [r0]

        ;*------------------------------------------------------
        ;* ESM clear: write 1 (clear) to every status bit in all
        ;* five status registers. Mirrors
        ;* ti.sysbios.family.arm.v7r.vim.Hwi's Hwi_initEsm().
        ;*------------------------------------------------------
        LDR   r2, esm_clear_value
        LDR   r0, esmsr1_addr
        STR   r2, [r0]
        LDR   r0, esmsr2_addr
        STR   r2, [r0]
        LDR   r0, esmsr3_addr
        STR   r2, [r0]
        LDR   r0, esmssr2_addr
        STR   r2, [r0]
        LDR   r0, esmsr4_addr
        STR   r2, [r0]

        ;*------------------------------------------------------
        ;* Standard C runtime init (.cinit/.bss), then main().
        ;* No RESET_FUNC/Startup_exec-style hook here -- this file
        ;* IS that hook, hand-written above, run before auto_init
        ;* the same way _c_int00 runs RESET_FUNC before auto_init.
        ;*------------------------------------------------------
        BL    __TI_auto_init
        BL    main

_hang:  B     _hang

;------------------------------------------------------------------------------
; Constant pool
;------------------------------------------------------------------------------
excstack_top_addr    .word _excStack+512
irqstack_top_addr     .word _irqStack+512
fiqstack_top_addr     .word _fiqStack+512
stack_base_addr       .word __stack
stack_size_addr       .word __STACK_SIZE
softrst2_addr          .word 0xFFFFFF08
vim_reset_assert_mask  .word 0xAD000000
vim_reset_clear_mask   .word 0xFF000000
esm_clear_value        .word 0xFFFFFFFF
esmsr1_addr            .word 0xFFFFF518
esmsr2_addr            .word 0xFFFFF51C
esmsr3_addr            .word 0xFFFFF520
esmssr2_addr           .word 0xFFFFF53C
esmsr4_addr            .word 0xFFFFF558

;------------------------------------------------------------------------------
; SVC/System stack (standard RTS boot convention symbols)
;------------------------------------------------------------------------------
        .global __stack
__stack: .usect ".stack", 0, 4
        .global __STACK_SIZE
__STACK_SIZE: .set 0x1000

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
