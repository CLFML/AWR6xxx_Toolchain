/*------------------------------------------------------------------------------
 * freertos_iwr68xx.c
 *
 * Interrupt-controller glue for FreeRTOS-Kernel's portable/GCC/ARM_CRx_No_GIC
 * port on the AWR6843 MSS. That port is generic across Cortex-R devices and
 * leaves interrupt-controller integration to the application; this file is
 * that integration for this device's VIM + RTI peripherals.
 *
 * Lives in this toolchain repo's shared src/ (alongside the shared
 * include/ device headers), not inside Universal_hal: this is specific to
 * *this toolchain's* choice of RTOS (FreeRTOS) and port
 * (ARM_CRx_No_GIC) for one specific chip, not a generic cross-platform HAL
 * concern the way Universal_hal's gpio/uart/spi/dma/mailbox modules are --
 * Universal_hal supports atmelsam and raspberrypi too and has no business
 * depending on FreeRTOS. Any example in this toolchain that uses FreeRTOS
 * on this chip (currently just GCC_FreeRTOS_MSS) adds this file directly
 * to its own executable's sources -- see that project's CMakeLists.txt.
 * Application-level FreeRTOS hooks tied to one specific board's pin
 * choices (fault-reporting diagnostics, stack-overflow/malloc-failed
 * reporting) stay in that project's own src/, not here.
 *
 * Register sequence ported from TI SYS/BIOS's own drivers for these two
 * peripherals -- the same ones SYS/BIOS itself uses for VIM bring-up and OS
 * ticking on this device family (bios_6_73_01_01):
 *   - ti/sysbios/family/arm/v7r/vim/Hwi.xs for the VIM base address
 *     (0xFFFFFDEC) and Hwi.c's Hwi_clearInterrupt() for the VIM-level ack
 *     mechanism (write the channel's bit to INTREQ[index]).
 *   - ti/sysbios/timers/rti/Timer.c's Timer_start() for the RTI Compare0/
 *     FRC0 register poke sequence.
 * See AWR6843_VIM.h/AWR6843_RTI.h for the register layout/bit citations.
 * RTI_A specifically (not RTI_B) -- see AWR6843.h's RTIA/RTIB comment for
 * why: RTIB ("RTI With Digital Watchdog Timer") is on a separate,
 * disabled-by-default watchdog clock domain and bus-faults on access; RTIA
 * is the plain general-purpose RTI on the normal peripheral clock, meant
 * for exactly this kind of periodic-tick use. CONFIRMED WORKING on real
 * hardware after that fix -- see the AWR6xxx_Toolchain memory notes
 * (gcc_freertos_mss_port.md) for the full bring-up story.
 *
 * Scope: only ONE interrupt source is ever enabled here -- RTI Compare0,
 * used as the FreeRTOS tick. vApplicationIRQHandler() therefore doesn't
 * need to read VIM's IRQVECREG to dispatch by channel (its exact semantics
 * on this device weren't fully pinned down during porting -- see the
 * project's memory notes); it can simply assume every IRQ is the tick.
 * A future multi-source build (e.g. once SPI/mailbox interrupts are wired
 * up) will need real vectoring here.
 *----------------------------------------------------------------------------*/
#include <FreeRTOS.h>
#include <task.h>
#include <AWR6843.h>

/* portASM.S (ARM_CRx_No_GIC) writes an arbitrary value here at the end of
 * every IRQ -- see FreeRTOSConfig.h's configEOI_ADDRESS comment for why
 * this is a harmless dummy rather than a real register on this device. */
uint32_t g_freertos_dummy_eoi_reg;

void vConfigureTickInterrupt(void) {
    const uint32_t reload = configCPU_CLOCK_HZ / configTICK_RATE_HZ;

    /* RTI: stop counter 0, select FRC0 as Compare0's source, reset the
     * counter, set the compare value and auto-reload increment (RTIUDCP0)
     * for a periodic (not one-shot) tick, clear any stale pending flag,
     * enable Compare0's interrupt, start counter 0. */
    RTI_A->RTIGCTRL &= ~RTIGCTRL_CNT0EN;
    RTI_A->RTICOMPCTRL &= RTICOMPCTRL_SEL0_MASK;
    RTI_A->RTIUC0 = 0U;
    RTI_A->RTIFRC0 = 0U;
    RTI_A->RTICOMP0 = reload;
    RTI_A->RTIUDCP0 = reload;
    RTI_A->RTIINTFLAG = RTIINTFLAG_INT0;
    RTI_A->RTISETINTENA = RTISETINTENA_INT0;
    RTI_A->RTIGCTRL |= RTIGCTRL_CNT0EN;

    /* VIM: route the RTI Compare0 channel to IRQ (not FIQ), then enable it. */
    VIM->FIRQPR[0] &= ~(1UL << SOC_XWR68XX_MSS_RTI_COMPARE0_INT);
    VIM->REQENASET[0] = (1UL << SOC_XWR68XX_MSS_RTI_COMPARE0_INT);
}

void vClearTickInterrupt(void) {
    RTI_A->RTIINTFLAG = RTIINTFLAG_INT0;
}

/* Called by FreeRTOS_IRQ_Handler (portASM.S) for every IRQ. ulICCIAR is
 * whatever happened to be in r0 at the call site on this port -- not a
 * real interrupt-ID register read (that's a GIC IAR concept; this device
 * has no GIC) -- ignored here. See this file's header for why "assume
 * it's always the tick" is valid for this single-source build. */
void vApplicationIRQHandler(uint32_t ulICCIAR) {
    (void)ulICCIAR;

    FreeRTOS_Tick_Handler(); /* internally calls configCLEAR_TICK_INTERRUPT() */

    /* Ack at the VIM level too -- write the channel's own bit to
     * INTREQ[0], the exact mechanism SYS/BIOS's Hwi_clearInterrupt() uses
     * (AWR6843_VIM.h). Without this the VIM channel stays latched. */
    VIM->INTREQ[0] = (1UL << SOC_XWR68XX_MSS_RTI_COMPARE0_INT);
}
