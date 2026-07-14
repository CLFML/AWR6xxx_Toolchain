/*------------------------------------------------------------------------------
 * startup_awr6843.S
 *
 * Minimal bare-metal boot for the AWR6843 MSS (Cortex-R4F), GNU assembler
 * port of Barebones_MSS's src/startup_awr6843.asm (TI armcl syntax) -- same
 * CP15/register sequence, different assembler. See that file's header (and
 * the AWR6xxx_Toolchain memory notes) for the full history of why this
 * boots where two earlier bare-metal attempts didn't: SYS/BIOS's own
 * generated startup does two things a plain C runtime's reset handler
 * doesn't --
 *   - ti.sysbios.family.arm.exc.Exception's Module_startup initializes the
 *     Abort- and Undefined-mode banked stack pointers (SP_abt, SP_und)
 *     before main() ever runs. Leaving these uninitialized means the very
 *     first Data/Prefetch Abort's own exception-entry prologue (pushing
 *     return state) corrupts memory at a garbage address instead of
 *     cleanly entering a handler -- the leading explanation for the
 *     "first peripheral access data-aborts" symptom both earlier attempts
 *     hit.
 *   - ti.sysbios.family.arm.v7r.vim.Hwi's Module_startup does a VIM
 *     hardware soft-reset (SOFTRST2 register) and, gated by an errata flag
 *     for this part, an unconditional ESM clear (ESMSR1-4/ESMSSR2 <-
 *     0xFFFFFFFF).
 * This file replicates both, plus IRQ/FIQ-mode stacks (cheap insurance,
 * even though interrupts are never enabled here -- Universal_hal's
 * esm_init() needs SYS/BIOS's Hwi_create() and isn't used in this
 * bare-metal build).
 *
 * MPU: disabled here (unknown state at boot), then properly configured by
 * soc_init() (Universal_hal, the same proven 12-region table used by
 * Barebones_MSS) once main() runs -- not a simplified stand-in.
 *
 * GNU vs TI syntax differences worth flagging for anyone diffing this
 * against startup_awr6843.asm:
 *   - `.thumb_func`/`.type foo,%function` replace TI's `.thumbfunc`/`.def`;
 *     no `.asmfunc`/`.endasmfunc` wrapper (that's a TI-only construct).
 *   - `msr cpsr_cf, r0` reads the same in both assemblers (the _cf suffix
 *     selects the control+flags fields either way).
 *   - GNU as has no `.usect`/`-stack N` linker-size-directive convention;
 *     the exception stacks and the main SVC/System stack are both plain
 *     linker-script-provided symbols instead (`_estack` in
 *     linker_awr6843.ld) -- see that file's header for why there's no
 *     separate ".stack" output section here.
 *   - This file also does the .bss zeroing (and .data LMA->VMA copy --
 *     see linker_awr6843.ld's header for why that turned out to be needed
 *     after all) that TI's `__TI_auto_init` does automatically from its
 *     `.cinit` table -- GNU's C runtime here is minimal enough (no libc
 *     startup pulled in) that this file has to do both directly.
 *
 * FreeRTOS variant: the SVC and IRQ vector-table entries below point at
 * FreeRTOS_SVC_Handler/FreeRTOS_IRQ_Handler (FreeRTOS-Kernel's
 * portable/GCC/ARM_CRx_No_GIC/portASM.S) instead of the infinite-loop
 * placeholders Barebones_MSS/GCC_MSS use -- see this toolchain repo's
 * shared ../../src/freertos_iwr68xx.c for the VIM/RTI interrupt-controller
 * integration those handlers rely on. Neither
 * handler needs its own banked-mode stack beyond what's already set up
 * here: FreeRTOS_SVC_Handler's first instruction (SRSDB sp!, #SYS_MODE)
 * targets System mode's own SP, never SVC's; FreeRTOS_IRQ_Handler only
 * briefly touches IRQ mode's SP (two words, at entry, before switching to
 * SVC mode) -- the existing 512-byte _irqStack already covers that.
 *----------------------------------------------------------------------------*/
    .syntax unified
    .arm

    .global _resetEntry
    .extern FreeRTOS_SVC_Handler
    .extern FreeRTOS_IRQ_Handler

/*------------------------------------------------------------------------------
 * Interrupt vector table -> placed at 0x00000000 by the linker (.intvecs)
 *----------------------------------------------------------------------------*/
    .section .intvecs, "ax"
    .align 4
    .global _intvecs
_intvecs:
    ldr pc, reset_addr        /* 0x00 Reset */
    ldr pc, undef_addr        /* 0x04 Undefined instruction */
    ldr pc, svc_addr          /* 0x08 Software interrupt (SVC) */
    ldr pc, pabort_addr       /* 0x0C Prefetch abort */
    ldr pc, dabort_addr       /* 0x10 Data abort */
    .word 0                   /* 0x14 Reserved */
    ldr pc, irq_addr          /* 0x18 IRQ */
    ldr pc, fiq_addr          /* 0x1C FIQ */

reset_addr:  .word _resetEntry
undef_addr:  .word _undefEntry
svc_addr:    .word FreeRTOS_SVC_Handler
pabort_addr: .word _pabortEntry
dabort_addr: .word _dabortEntry
irq_addr:    .word FreeRTOS_IRQ_Handler
fiq_addr:    .word _fiqEntry

/*------------------------------------------------------------------------------
 * Per-mode exception stacks. One shared "exception stack" for Abort and
 * Undefined mode (they can't both be active at once -- matches
 * Exception_initCore0's own convention of pointing SP_abt and SP_und at
 * the same region), plus separate IRQ/FIQ stacks. 512 bytes each is
 * generous for this program (nothing here ever runs deeply nested, and no
 * interrupt is ever enabled), 8-byte aligned per EABI. Placed directly in
 * .bss (rather than via the ".lcomm symbol, size, align" pseudo-op -- this
 * target's assembler only accepts .lcomm's 2-argument form, no alignment)
 * so linker_awr6843.ld's __bss_start__/__bss_end__ zeroing loop covers
 * them too.
 *----------------------------------------------------------------------------*/
    .section .bss
    .align 3
    .global _excStack
_excStack:
    .space 512
    .align 3
    .global _irqStack
_irqStack:
    .space 512
    .align 3
    .global _fiqStack
_fiqStack:
    .space 512

/*------------------------------------------------------------------------------
 * Reset trampoline.
 *----------------------------------------------------------------------------*/
    .text
    .arm
    .type _resetEntry, %function
_resetEntry:
    /*------------------------------------------------------
     * Disable the MPU -- unknown state at boot; soc_init()
     * (called from main(), see file header) configures it
     * properly from this known-disabled starting point.
     *------------------------------------------------------*/
    mrc   p15, #0x00, r0, c1, c0, #0x00   /* read SCTLR */
    bic   r0, r0, #0x00000001              /* clear M (MPU enable) bit */
    mcr   p15, #0x00, r0, c1, c0, #0x00   /* write SCTLR back */
    isb

    /*------------------------------------------------------
     * Enable the VFP coprocessor: full CP10/CP11 access via
     * CPACR, then FPEXC.EN. GCC-R4F-toolchain.cmake builds
     * with -mfloat-abi=hard (-mfpu=vfpv3-d16), so any `float`
     * arithmetic emits real VFP instructions directly -- the
     * first one executed without this traps as an Undefined
     * Instruction exception. This project itself never uses
     * float today, but GCC_FreeRTOS_VitalSigns_MSS (same
     * compile flags, same startup file lineage) does, and hit
     * exactly that on real hardware (repeating "UNDEFINED
     * INSTRUCTION" the moment a float-parsing CLI command ran)
     * before this step was added there -- fixed here too so
     * the gap doesn't resurface the next time this project (or
     * a copy of it) starts using floating point.
     *------------------------------------------------------*/
    mrc   p15, #0, r0, c1, c0, #2   /* read CPACR */
    orr   r0, r0, #(0xF << 20)      /* full access to CP10/CP11 */
    mcr   p15, #0, r0, c1, c0, #2   /* write CPACR */
    isb
    mov   r0, #0x40000000           /* FPEXC.EN */
    vmsr  fpexc, r0

    /*------------------------------------------------------
     * Abort mode: set SP_abt (shared exception stack)
     *------------------------------------------------------*/
    mrs   r0, cpsr
    bic   r0, r0, #0x1F
    orr   r0, r0, #0x17     /* Abort mode */
    msr   cpsr_cf, r0
    ldr   sp, excstack_top_addr

    /*------------------------------------------------------
     * Undefined mode: set SP_und (same shared exception stack)
     *------------------------------------------------------*/
    mrs   r0, cpsr
    bic   r0, r0, #0x1F
    orr   r0, r0, #0x1B     /* Undefined mode */
    msr   cpsr_cf, r0
    ldr   sp, excstack_top_addr

    /*------------------------------------------------------
     * IRQ mode: set SP_irq
     *------------------------------------------------------*/
    mrs   r0, cpsr
    bic   r0, r0, #0x1F
    orr   r0, r0, #0x12     /* IRQ mode */
    msr   cpsr_cf, r0
    ldr   sp, irqstack_top_addr

    /*------------------------------------------------------
     * FIQ mode: set SP_fiq
     *------------------------------------------------------*/
    mrs   r0, cpsr
    bic   r0, r0, #0x1F
    orr   r0, r0, #0x11     /* FIQ mode */
    msr   cpsr_cf, r0
    ldr   sp, fiqstack_top_addr

    /*------------------------------------------------------
     * Back to System mode for the main SVC/System stack
     * (shared with User mode banking; this program never
     * enters User mode). _estack is linker-provided, see
     * linker_awr6843.ld.
     *------------------------------------------------------*/
    mrs   r0, cpsr
    bic   r0, r0, #0x1F
    orr   r0, r0, #0x1F     /* System mode */
    msr   cpsr_cf, r0
    ldr   sp, estack_addr

    /*------------------------------------------------------
     * VIM hardware soft-reset (SOFTRST2, RCM_BASE + 0x08).
     * Mirrors ti.sysbios.family.arm.v7r.vim.Hwi's
     * Hwi_initIntController(): assert VIM-only reset, then
     * de-assert. No interrupts are enabled in this program,
     * but this puts the VIM peripheral itself into the same
     * known state SYS/BIOS's startup would.
     *------------------------------------------------------*/
    ldr   r0, softrst2_addr
    ldr   r1, [r0]
    ldr   r2, vim_reset_assert_mask
    orr   r1, r1, r2
    str   r1, [r0]
    ldr   r2, vim_reset_clear_mask
    bic   r1, r1, r2
    str   r1, [r0]

    /*------------------------------------------------------
     * ESM clear: write 1 (clear) to every status bit in all
     * five status registers. Mirrors
     * ti.sysbios.family.arm.v7r.vim.Hwi's Hwi_initEsm().
     *------------------------------------------------------*/
    ldr   r2, esm_clear_value
    ldr   r0, esmsr1_addr
    str   r2, [r0]
    ldr   r0, esmsr2_addr
    str   r2, [r0]
    ldr   r0, esmsr3_addr
    str   r2, [r0]
    ldr   r0, esmssr2_addr
    str   r2, [r0]
    ldr   r0, esmsr4_addr
    str   r2, [r0]

    /*------------------------------------------------------
     * Copy .data from its load address (PROG_RAM) to its run
     * address (DATA_RAM/TCM) -- see linker_awr6843.ld's header
     * for why this is needed (unlike .text/.rodata, which the
     * SBL loads directly to their single VMA==LMA address).
     *------------------------------------------------------*/
    ldr   r0, data_load_addr
    ldr   r1, data_start_addr
    ldr   r2, data_end_addr
copy_data_loop:
    cmp   r1, r2
    bge   copy_data_done
    ldr   r3, [r0], #4
    str   r3, [r1], #4
    b     copy_data_loop
copy_data_done:

    /*------------------------------------------------------
     * Zero .bss.
     *------------------------------------------------------*/
    ldr   r0, bss_start_addr
    ldr   r1, bss_end_addr
    mov   r2, #0
zero_bss_loop:
    cmp   r0, r1
    bge   zero_bss_done
    str   r2, [r0], #4
    b     zero_bss_loop
zero_bss_done:

    /*------------------------------------------------------
     * No RESET_FUNC/Startup_exec-style hook here -- this
     * file IS that hook, hand-written above, run before
     * main() the same way SYS/BIOS's _c_int00 runs
     * RESET_FUNC/Startup_exec before main().
     *------------------------------------------------------*/
    bl    main

_hang:
    b     _hang

/*------------------------------------------------------------------------------
 * Constant pool
 *----------------------------------------------------------------------------*/
excstack_top_addr:    .word _excStack + 512
irqstack_top_addr:    .word _irqStack + 512
fiqstack_top_addr:    .word _fiqStack + 512
estack_addr:           .word _estack
data_load_addr:         .word __data_load__
data_start_addr:         .word __data_start__
data_end_addr:            .word __data_end__
softrst2_addr:              .word 0xFFFFFF08
vim_reset_assert_mask:  .word 0xAD000000
vim_reset_clear_mask:   .word 0xFF000000
esm_clear_value:        .word 0xFFFFFFFF
esmsr1_addr:            .word 0xFFFFF518
esmsr2_addr:            .word 0xFFFFF51C
esmsr3_addr:            .word 0xFFFFF520
esmssr2_addr:           .word 0xFFFFF53C
esmsr4_addr:            .word 0xFFFFF558
bss_start_addr:         .word __bss_start__
bss_end_addr:            .word __bss_end__

/*------------------------------------------------------------------------------
 * Exception handlers. SVC and IRQ are handled by FreeRTOS_SVC_Handler/
 * FreeRTOS_IRQ_Handler instead (see vector table above) -- FIQ is never
 * enabled by this port, so it keeps a silent spin-in-place placeholder
 * like the non-RTOS examples (a debugger, if attached, will stop there
 * instead of running off into RAM).
 *
 * Undefined/Prefetch-Abort call vDiagFaultHandler(); Data-Abort calls the
 * more detailed vDiagDataAbort() (reads CP15 DFAR/DFSR + the faulting PC
 * first) -- both in src/freertos_glue.c, this project's own board-specific
 * diagnostics (LED blink + a plain-text message repeated over UART),
 * rather than spinning silently. A silent spin here was indistinguishable
 * at the LED from any other hang during this port's hardware bring-up --
 * this exact instrumentation is what actually found the real bug (an
 * RTIA/RTIB register-address mixup, see the AWR6xxx_Toolchain memory
 * notes, gcc_freertos_mss_port.md, for the full story). Safe to call a C
 * function from here: Abort and Undefined modes share the dedicated
 * _excStack set up in _resetEntry, valid regardless of what corrupted the
 * interrupted code's own stack.
 *----------------------------------------------------------------------------*/
    .extern vDiagFaultHandler
    .extern vDiagDataAbort

_undefEntry:
    mov   r0, #6
    bl    vDiagFaultHandler
_pabortEntry:
    mov   r0, #7
    bl    vDiagFaultHandler
_dabortEntry:
    /* A Data Abort's LR_abt = faulting instruction address + 8 (ARM
     * architecture convention) -- subtract 8 to get the actual PC.
     * DFAR/DFSR (CP15 c6/c0/0 and c5/c0/0) give the faulting address and
     * fault-type/status, matching a real "why did this happen" report
     * instead of just "somewhere in vTaskDelay()" -- see
     * gcc_freertos_mss_port.md in the AWR6xxx_Toolchain memory notes: this
     * abort was reproduced repeatedly but never pinned down further
     * through code review alone until this diagnostic was added. */
    mrc   p15, #0, r0, c6, c0, #0   /* r0 = DFAR (faulting address) */
    mrc   p15, #0, r1, c5, c0, #0   /* r1 = DFSR (fault status) */
    mov   r2, lr
    sub   r2, r2, #8                /* r2 = faulting instruction address */
    bl    vDiagDataAbort
_fiqEntry:    b _fiqEntry

    .end
