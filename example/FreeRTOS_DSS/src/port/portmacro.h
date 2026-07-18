/*
 * portmacro.h -- FreeRTOS port for the AWR6843 DSS (C674x/C64x+ DSP),
 * built with TI's cl6x (no C6000 GCC backend exists). Adapted from TI's
 * own ProcessorSDK PDK FreeRTOS port for C66x
 * (ti/kernel/freertos/portable/TI_CGT/c66/portmacro.h) -- see
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes for the full
 * story of what's reused vs. rewritten and why.
 *
 * Task switching (portmacro types, StackType_t, stack growth direction,
 * yield/critical-section macro NAMES) matches that PDK port exactly, since
 * it reuses the SAME underlying SYS/BIOS family.c62 TaskSupport assembly
 * (confirmed byte-identical -- see TaskSupport_asm.s62 in this directory,
 * copied verbatim from ~/ti/bios_6_73_01_01/packages/ti/sysbios/family/c62/).
 *
 * Deliberately does NOT reuse the PDK port's ti/osal-based
 * portSET_INTERRUPT_MASK_FROM_ISR (HwiP_disable/restore) or its
 * Hwi_disp_always.s64P-based general N-vector interrupt dispatch --
 * neither has AWR68xx/c674x support, and this port only needs ONE
 * interrupt source (the tick timer) for now, which cl6x's own `interrupt`
 * C function qualifier handles safely with compiler-generated (not
 * hand-written) save/restore -- see port_c674.c.
 */
#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Type definitions. */
#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short
#define portSTACK_TYPE  uint32_t
#define portBASE_TYPE   long

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

typedef uint32_t TickType_t;
#define portMAX_DELAY (TickType_t)0xFFFFFFFFUL

/* Hardware specifics -- TaskSupport_asm.s62's buildTaskStack pushes
 * downward (STACK--), a full-descending stack, same as the PDK port.
 * portBYTE_ALIGNMENT: C6000 EABI requires 8-byte stack alignment (see
 * TaskSupport_asm.s62's own SWREGS-must-be-even comment). */
#define portSTACK_GROWTH (-1)
#define portTICK_PERIOD_MS ((TickType_t)1000U / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT 8U
#define portHAS_STACK_OVERFLOW_CHECKING 1
#define portCRITICAL_NESTING_IN_TCB 1

/* Task switch utilities -- see port_c674.c. */
extern void vPortYeildFromISR(uint32_t x);
extern void vPortYield(void);
extern void vPortYieldAsyncFromISR(void);
#define portYIELD_FROM_ISR(x)    vPortYeildFromISR(x)
#define portEND_SWITCHING_ISR(x) vPortYeildFromISR(x)
#define portYIELD()               vPortYield()
extern void vPortAssertIfInISR(void);
#define portASSERT_IF_IN_ISR() vPortAssertIfInISR()

/* Critical section control -- plain compiler intrinsics (cl6x's
 * _disable_interrupts()/_enable_interrupts()/_restore_interrupts(),
 * standard C6000 CSR.GIE access, see c6x.h), not ti/osal's HwiP. Nested
 * critical sections from within an ISR use the SAME primitives here --
 * this port has no separate "ISR mask level" concept, unlike the PDK
 * port's HwiP-based one, since we only ever run at a single interrupt
 * priority (see port_c674.c's header for why that's fine for this port's
 * current one-interrupt-source scope). */
extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);
#define portENTER_CRITICAL()   vPortEnterCritical()
#define portEXIT_CRITICAL()    vPortExitCritical()
#define portDISABLE_INTERRUPTS() _disable_interrupts()
#define portENABLE_INTERRUPTS()  _enable_interrupts()
#define portSET_INTERRUPT_MASK_FROM_ISR()      _disable_interrupts()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)   _restore_interrupts(x)

/* Task function macros as described on the FreeRTOS.org web site -- not
 * required by this port but included in case common demo code uses them. */
#define portTASK_FUNCTION_PROTO(vFunction, pvParameters) void vFunction(void *pvParameters)
#define portTASK_FUNCTION(vFunction, pvParameters)       void vFunction(void *pvParameters)

/* This DSS build never touches the FPU (dss_main.c under
 * VITALSIGNS_TESTBENCH_MODE does no floating-point work -- see
 * ti_rtos_dss_replica.md), so unlike the PDK port this doesn't wire up an
 * FPU-context-save path; if a future phase needs float, add it the same
 * way GCC_FreeRTOS_VitalSigns_MSS's startup file had to for the ARM side. */

#if (configUSE_PORT_OPTIMISED_TASK_SELECTION == 1)
#define portRECORD_READY_PRIORITY(uxPriority, uxReadyPriorities) (uxReadyPriorities) |= (1UL << (uxPriority))
#define portRESET_READY_PRIORITY(uxPriority, uxReadyPriorities)  (uxReadyPriorities) &= ~(1UL << (uxPriority))
#define portGET_HIGHEST_PRIORITY(uxTopPriority, uxReadyPriorities) uxTopPriority = (31 - _lmbd(1, uxReadyPriorities))
#endif

extern BaseType_t xPortInIsrContext(void);

/* _mfence is a C66x-only intrinsic (SIMD/multi-core memory fence) --
 * doesn't exist on C674x/C64x+ (confirmed: cl6x treats it as an unknown
 * external call, not an intrinsic, and it's unresolved at link time). Not
 * needed here anyway: C674x is a single, in-order core with no SMP/
 * out-of-order-memory concerns at this level -- FreeRTOS's own use of
 * `volatile` on shared state is sufficient. */
#define portMEMORY_BARRIER() ((void)0)

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
