/*
 * port_c674.c -- FreeRTOS port layer for the AWR6843 DSS (C674x/C64x+).
 * See portmacro.h's header for the overall design (reuses SYS/BIOS's own
 * family.c62 TaskSupport assembly for context switching, does NOT reuse
 * SYS/BIOS's general Hwi dispatch table or TI's ti/osal -- neither has
 * AWR68xx support).
 *
 * Structurally adapted from TI's ProcessorSDK PDK FreeRTOS port for C66x
 * (ti/kernel/freertos/portable/TI_CGT/c66/port_c66.c): task-stack-build/
 * yield/scheduler-start functions follow that file's proven pattern
 * closely (same TaskSupport_buildTaskStack/_swap__E calls, same
 * ulPortYieldRequired/ulPortInterruptNesting bookkeeping), with its
 * ti/osal- and SYS/BIOS-Hwi-module-specific pieces (TimerP, CacheP,
 * Hwi_Module_startup, Hwi_switchFromBootStack) replaced by this project's
 * own dss_tick_timer.c (RTI timer + INTMUX setup, see that file).
 */
#include <stdint.h>
#include <stdio.h>
#include <c6x.h>
#include <FreeRTOS.h>
#include <task.h>

#define portNO_CRITICAL_NESTING ((uint32_t)0)

volatile uint32_t ulCriticalNesting = 9999UL;
BaseType_t ulPortYieldRequired = pdFALSE;
BaseType_t ulPortInterruptNesting = pdFALSE;
BaseType_t ulPortSchedularRunning = pdFALSE;
BaseType_t uxPortIncorrectYieldCount = 0UL;

/* Only the first member of the real TCB matters here -- FreeRTOS's own
 * tasks.c guarantees pxTopOfStack is always the TCB's first member
 * ("THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT", see tasks.c), so a
 * struct with just that one field gives identical pointer arithmetic to
 * casting the real (much larger, config-conditional) TCB_t -- avoids
 * vendoring a second copy of that conditional layout here that could
 * silently drift out of sync with tasks.c's own. */
typedef struct {
    volatile StackType_t *pxTopOfStack;
} tskTCB;

extern tskTCB *volatile pxCurrentTCB;

extern void dss_tick_timer_start(void);

/* Temporary diagnostic: expose pxTopOfStack for any TaskHandle_t so main.c
 * can stash it right after xTaskCreate() returns -- i.e. BEFORE the task
 * has ever run even once -- to check whether pxPortInitialiseStack()/
 * TaskSupport_buildTaskStack() computed a sane initial stack pointer for
 * every task, not just whichever one happens to be pxCurrentTCB when
 * xPortStartScheduler() first runs (Task_enter()'s own SP capture only
 * covers THAT one task). See dss_freertos_port.md in the
 * AWR6xxx_Toolchain memory notes. */
uint32_t dss_debug_peek_stack(TaskHandle_t h) {
    return (uint32_t)(((tskTCB *)h)->pxTopOfStack);
}

#define DSS_DIAG_PXCURRENTTCB_ADDRESS     0x21080058U
#define DSS_DIAG_PXCURRENTTCB_TOS_ADDRESS 0x2108005CU
/* Temporary diagnostic: capture SP right after enabling interrupts on the
 * BOOT stack (before any task has ever been dispatched), so a hardware
 * capture can directly confirm this is sane/large (the linker's own
 * .stack section) rather than a small per-task allocation. See
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes. */
extern void dss_capture_sp(uint32_t *dst);
#define DSS_DIAG_BOOT_ENABLE_SP_ADDRESS 0x21080094U

static void prvTaskExitError(void) {
    /* configASSERT(pdFALSE) itself never returns (see FreeRTOSConfig.h) --
     * a task function must never fall off its own end. */
    configASSERT(pdFALSE);
}

#define TaskSupport_buildTaskStack ti_sysbios_family_c62_TaskSupport_buildTaskStack

typedef void (*Task_FuncPtr)(uint32_t arg1, uint32_t arg2);
typedef void (*TaskSupport_FuncPtr)(void);

extern void *TaskSupport_buildTaskStack(void *stack, Task_FuncPtr fxn, TaskSupport_FuncPtr exit,
                                         TaskSupport_FuncPtr enter, uint32_t arg0, uint32_t arg1);

void Task_exit(void) {
    prvTaskExitError();
}

/* Temporary diagnostic: stash CSR right after portENABLE_INTERRUPTS() so
 * MSS's dssPeek can confirm CSR.GIE (bit0) actually got set -- see
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes. */
#define DSS_DIAG_CSR_READBACK_ADDRESS 0x21080014U
/* Temporary diagnostic: capture B15 (SP) as the very first task starts
 * running, BEFORE portENABLE_INTERRUPTS() -- to tell apart "the stack
 * pointer was already broken by the context-switch/stack-build path,
 * before any interrupt could possibly touch it" from "SP was fine here
 * and got corrupted later, during normal task execution". cl6x has no
 * GCC-style named-register variable extension, so this is a tiny hand
 * written assembly helper (see dss_capture_sp in startup_dss.asm) rather
 * than inline asm. See dss_freertos_port.md in the AWR6xxx_Toolchain
 * memory notes. */
extern void dss_capture_sp(uint32_t *dst);
extern void dss_nop_delay(void);

/* Task_enter() is the SHARED bootstrap entry every task's first-ever
 * dispatch goes through (idle, the FreeRTOS timer-service task, and both
 * counter tasks) -- a single fixed-address SP capture only shows
 * whichever one ran LAST, not the full sequence. Log up to 4 entries
 * instead: [pxCurrentTCB, SP] pairs, so each bootstrapped task's identity
 * and SP are both visible. See dss_freertos_port.md in the
 * AWR6xxx_Toolchain memory notes. */
#define DSS_DIAG_ENTER_LOG_INDEX_ADDRESS 0x21080070U
#define DSS_DIAG_ENTER_LOG_BASE_ADDRESS  0x21080074U /* 4 * (tcb,sp) = 32 bytes */

/* portENABLE_INTERRUPTS() lives HERE, not in xPortStartScheduler() --
 * matching the PDK's own proven C66x reference port (port_c66.c's
 * Task_enter(), which does exactly this) rather than real SYS/BIOS's
 * Hwi_startup() ordering (an earlier attempt at "matching the proven
 * reference" that used the wrong reference -- see
 * xPortStartScheduler()'s own comment and dss_freertos_port.md's
 * Attempts 3 and 8-12 in the AWR6xxx_Toolchain memory notes for the full
 * story). By the time Task_enter() runs, vPortRestoreTaskContext()'s
 * critical-section-protected switch machinery has already completed on
 * the BOOT stack -- enabling interrupts only now, on the new task's own
 * stack, after that machinery is done, avoids taking an interrupt while
 * mid-switch and avoids vPortEnterCritical()/vPortExitCritical()'s
 * nesting counter (which starts at 9999, see ulCriticalNesting) ever
 * getting in the way of this unconditional, direct enable. */
void Task_enter(void) {
    volatile uint32_t *idx = (volatile uint32_t *)DSS_DIAG_ENTER_LOG_INDEX_ADDRESS;
    uint32_t slot = *idx;
    uint32_t clampedSlot = (slot < 4U) ? slot : 3U;
    uint32_t *entry = (uint32_t *)(DSS_DIAG_ENTER_LOG_BASE_ADDRESS + clampedSlot * 8U);
    entry[0] = (uint32_t)pxCurrentTCB;
    if (slot < 4U) {
        *idx = slot + 1U;
    }
    dss_capture_sp(&entry[1]);
    *(volatile uint32_t *)DSS_DIAG_CSR_READBACK_ADDRESS = CSR;

    /* Attempt 14 (dss_freertos_port.md, AWR6xxx_Toolchain memory notes)
     * tried making this ICR clear one-time-only (a static guard), on the
     * theory that repeating it on every task's entry could discard a
     * genuinely fresh pending tick, not just the original stale one --
     * that version was NEVER observed to reach INT14_VEC successfully in
     * any subsequent test (JTAG race tests all timed out/never-taken).
     * Reverted back to the unconditional clear on every call -- the
     * ORIGINAL Attempt 13 build (unconditional) was the one a 6/6 JTAG
     * race test confirmed landing correctly at INT14_VEC every time, so
     * the one-time-guard change looks like a regression, not a fix. */
    ICR = (1U << 14U); /* DSS_TICK_VECTOR_ID, see dss_tick_timer.c */
    portENABLE_INTERRUPTS();
    dss_capture_sp((uint32_t *)DSS_DIAG_BOOT_ENABLE_SP_ADDRESS);
}

void ti_sysbios_knl_Task_Func(uint32_t arg1, uint32_t arg2) {
    TaskFunction_t pxCode = (TaskFunction_t)arg2;
    pxCode((void *)arg1);
}

/* portHAS_STACK_OVERFLOW_CHECKING is 1 (portmacro.h), so this port's
 * pxPortInitialiseStack() takes the extended 4-argument form (pxEndOfStack
 * included) -- matches portable.h's declaration for that config, same as
 * the PDK port_c66.c's own #if (1 == portHAS_STACK_OVERFLOW_CHECKING)
 * branch. pxEndOfStack isn't otherwise used here (kernel-side stack-limit
 * checking against it is a FreeRTOS-generic feature; this port doesn't
 * additionally use it in this function itself). */
StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack, StackType_t *pxEndOfStack, TaskFunction_t pxCode,
                                    void *pvParameters) {
    (void)pxEndOfStack;
    pxTopOfStack = (StackType_t *)TaskSupport_buildTaskStack(pxTopOfStack, ti_sysbios_knl_Task_Func, Task_exit,
                                                              Task_enter, (uint32_t)pvParameters, (uint32_t)pxCode);
    return pxTopOfStack;
}

extern void ti_sysbios_family_c62_TaskSupport_swap__E(void **oldtskContext, void **newtskContext);

void vPortRestoreTaskContext(void) {
    void *dummyTaskSp;
    ti_sysbios_family_c62_TaskSupport_swap__E(&dummyTaskSp, (void **)(&pxCurrentTCB->pxTopOfStack));
}

BaseType_t xPortStartScheduler(void) {
    portDISABLE_INTERRUPTS();

    /* Programs the RTI tick timer + maps its event onto a CPU interrupt
     * vector (see dss_tick_timer.c) -- the equivalent of the PDK port's
     * Hwi_Module_startup()+TimerP_create()/start(), just talking to the
     * hardware directly instead of through SYS/BIOS's Hwi module or
     * ti/osal's TimerP. */
    dss_tick_timer_start();

    ulPortSchedularRunning = pdTRUE;

    *(volatile uint32_t *)DSS_DIAG_PXCURRENTTCB_ADDRESS = (uint32_t)pxCurrentTCB;
    *(volatile uint32_t *)DSS_DIAG_PXCURRENTTCB_TOS_ADDRESS = (uint32_t)(pxCurrentTCB->pxTopOfStack);

    /* Interrupts are NOT enabled here -- see Task_enter()'s own comment.
     * ulCriticalNesting starts at 9999 (see its declaration) specifically
     * so that vPortEnterCritical()/vPortExitCritical() calls occurring
     * during vPortRestoreTaskContext() below (TaskSupport_swap__E calls
     * into vTaskSwitchContext(), which uses real critical sections) never
     * accidentally trip the "nesting == 0" re-enable path in
     * vPortExitCritical() -- they just hover near 9999, nowhere close to
     * 0. Enabling interrupts unconditionally here, BEFORE that
     * critical-section-protected switch machinery runs, was tried
     * (matching real SYS/BIOS's Hwi_startup() ordering) and is exactly
     * what caused both a nondeterministic hijack into INT15_VEC AND,
     * once that was papered over, GIE getting disabled by
     * vPortEnterCritical() during the switch and never re-enabled again
     * (nesting never returns to exactly 0 relative to its 9999 start) --
     * see dss_freertos_port.md's Attempts 8-12 (AWR6xxx_Toolchain memory
     * notes) for the full trace that found this. The PDK's own C66x
     * reference port (port_c66.c) never enables interrupts in
     * xPortStartScheduler() either, for exactly this reason -- it does it
     * in Task_enter(), AFTER the switch machinery has already run to
     * completion on the new task's own stack. */

    /* Start the first task executing -- never returns. */
    vPortRestoreTaskContext();

    (void)prvTaskExitError;
    return pdTRUE;
}

void vPortYeildFromISR(uint32_t xSwitchRequired) {
    if (pdFALSE != xSwitchRequired) {
        ulPortYieldRequired = pdTRUE;
    }
}

void vPortTimerTickHandler(void) {
    if (pdTRUE == ulPortSchedularRunning) {
        if (pdFALSE != xTaskIncrementTick()) {
            ulPortYieldRequired = pdTRUE;
        }
    }
}

void vPortConfigTimerForRunTimeStats(void) {
}

void vPortValidateInterruptPriority(void) {
}

void vPortEndScheduler(void) {
    /* Nothing to do -- interrupts are disabled by FreeRTOS before calling
     * this, same as the PDK port's own vPortEndScheduler(). */
}

/* Unlike the PDK port's vPortYield() (which only re-enables interrupts
 * when the newly-scheduled task's own uxCriticalNesting is 0), this port
 * re-enables unconditionally: the minimal tskTCB above doesn't track
 * per-task critical nesting (see its own comment for why), and this
 * port's only caller of portYIELD() is voluntary task code that isn't
 * itself inside a critical section (vTaskEnterCritical()/
 * portENTER_CRITICAL() never call vPortYield()). */
void vPortYield(void) {
    void **oldSP;
    void **newSP;

    portDISABLE_INTERRUPTS();
    oldSP = (void **)(&pxCurrentTCB->pxTopOfStack);
    vTaskSwitchContext();
    newSP = (void **)(&pxCurrentTCB->pxTopOfStack);
    if (oldSP != newSP) {
        ti_sysbios_family_c62_TaskSupport_swap__E(oldSP, newSP);
    } else {
        uxPortIncorrectYieldCount++;
    }
    portENABLE_INTERRUPTS();
}

void vPortYieldAsyncFromISR(void) {
    void **oldSP;
    void **newSP;

    oldSP = (void **)(&pxCurrentTCB->pxTopOfStack);
    vTaskSwitchContext();
    newSP = (void **)(&pxCurrentTCB->pxTopOfStack);
    if (oldSP != newSP) {
        ti_sysbios_family_c62_TaskSupport_swap__E(oldSP, newSP);
    } else {
        uxPortIncorrectYieldCount++;
    }
    portDISABLE_INTERRUPTS();
}

BaseType_t xPortInIsrContext(void) {
    return (pdFALSE != ulPortInterruptNesting) ? pdTRUE : pdFALSE;
}

void vPortAssertIfInISR(void) {
    configASSERT(!xPortInIsrContext());
}

/* Simple nesting-counted critical section -- unlike the PDK port (which
 * delegates to ti/osal's HwiP), this port has no separate ISR-priority-
 * masking concept, since dss_tick_timer.c's ISR is this port's only
 * interrupt source for now (see portmacro.h's header). */
void vPortEnterCritical(void) {
    portDISABLE_INTERRUPTS();
    ulCriticalNesting++;
}

void vPortExitCritical(void) {
    if (ulCriticalNesting > portNO_CRITICAL_NESTING) {
        ulCriticalNesting--;
        if (ulCriticalNesting == portNO_CRITICAL_NESTING) {
            portENABLE_INTERRUPTS();
        }
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    configASSERT(pdFALSE);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *puxIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize) {
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/* Temporary diagnostic: increments on every idle-task loop iteration --
 * lets MSS's dssPeek confirm the scheduler genuinely reaches idle (both
 * counter tasks correctly blocked in vTaskDelay()) rather than being stuck
 * somewhere inside the second task's own vTaskDelay()/yield call before
 * ever getting there. See dss_freertos_port.md in the AWR6xxx_Toolchain
 * memory notes. */
#define DSS_DIAG_IDLE_HITCOUNT_ADDRESS 0x21080028U

/* Diagnostic: the idle hit-count (cleanly 1, frozen -- see
 * dss_freertos_port.md) proved the scheduler genuinely reaches idle
 * exactly once, then never resumes -- isolating the problem to the
 * `IDLE` instruction's own sleep/wake condition specifically, not
 * interrupt delivery in general (every register gating delivery was
 * already independently confirmed correct). Testing with `IDLE` removed
 * (busy-spin instead of true CPU sleep) to confirm this directly: if the
 * tick now advances, the wake condition -- not delivery -- was the
 * remaining gap. */
/* Temporary diagnostic: live, continuously-updated IER/IFR/CSR readback --
 * lets MSS's dssPeek see their CURRENT state during the busy-spin, not
 * just a one-time snapshot captured back in dss_tick_timer_start() before
 * GIE ever went live. See dss_freertos_port.md in the AWR6xxx_Toolchain
 * memory notes. */
#define DSS_DIAG_LIVE_IER_ADDRESS 0x2108002CU
#define DSS_DIAG_LIVE_IFR_ADDRESS 0x21080030U
#define DSS_DIAG_LIVE_CSR_ADDRESS 0x21080034U

void vApplicationIdleHook(void) {
    volatile uint32_t *idleHits = (volatile uint32_t *)DSS_DIAG_IDLE_HITCOUNT_ADDRESS;
    (*idleHits)++;
    *(volatile uint32_t *)DSS_DIAG_LIVE_IER_ADDRESS = IER;
    *(volatile uint32_t *)DSS_DIAG_LIVE_IFR_ADDRESS = IFR;
    *(volatile uint32_t *)DSS_DIAG_LIVE_CSR_ADDRESS = CSR;
}
