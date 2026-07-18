/*
 * main.c -- FreeRTOS_DSS phase-1 bring-up: two tasks incrementing
 * separate HSRAM counters on independent vTaskDelay() periods. Not
 * dss_main.c's application logic yet (see this repo's memory notes,
 * dss_freertos_port.md) -- this only proves the port layer itself
 * (scheduler start, tick-driven preemption, task switching) before that
 * gets built on top.
 *
 * Reuses the same alive-check-style verification approach
 * example/GCC_FreeRTOS_DSS_Probe_MSS already has tooling for: MSS can
 * dssPeek these two DSS-side addresses (0x21080000, 0x21080004 -- HSRAM,
 * same window mmw_messages.h's MMWDEMO_DSS_ALIVE_CHECK_* already uses)
 * and watch both counters increment independently to confirm both tasks
 * are actually being scheduled/preempted, not just that boot reached
 * main().
 */
#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>

/* L1P instruction cache global invalidate + L1D writeback-invalidate --
 * same registers SYS/BIOS's own Cache_invL1pAll()/Cache_wbInvAll() use
 * (~/ti/bios_6_73_01_01/packages/ti/sysbios/family/c64p/Cache.c). Called
 * as the very first thing in main(): this project's whole development
 * loop repeatedly overwrites DSS's L2 memory via MSS's dssLoad (see
 * example/GCC_FreeRTOS_DSS_Probe_MSS), including the interrupt vector
 * table, across MANY iterations in the same debug session -- and a
 * SOFTSYSRST warm chip reset (this project's own `reset` CLI command)
 * resets CPU/peripheral state but does NOT necessarily clear L1P's cache
 * SRAM. Without this, the CPU can silently fetch STALE cached
 * instructions for a vector table address that was legitimately
 * overwritten with new content moments earlier -- every hardware register
 * involved in interrupt delivery can check out perfectly correct
 * (IFR/IER/CSR.GIE/INTMUX/ISTP all confirmed via readback) while the
 * actual fetched vector-table bytes are still an old iteration's,
 * explaining a "pending+enabled interrupt that never executes" symptom
 * that's otherwise inexplicable from register state alone. See
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes. */
#define L1PINV   ((volatile uint32_t *)0x01845028U)
#define L1DWBINV ((volatile uint32_t *)0x01845044U)

static void dss_cache_invalidate_all(void) {
    *L1DWBINV = 1U;
    *L1PINV = 1U;
}

#define TASK_A_COUNTER_ADDRESS 0x21080000U
#define TASK_B_COUNTER_ADDRESS 0x21080004U

static void vCounterTaskA(void *pvParameters) {
    volatile uint32_t *counter = (volatile uint32_t *)TASK_A_COUNTER_ADDRESS;
    (void)pvParameters;
    *counter = 0;
    for (;;) {
        (*counter)++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void vCounterTaskB(void *pvParameters) {
    volatile uint32_t *counter = (volatile uint32_t *)TASK_B_COUNTER_ADDRESS;
    (void)pvParameters;
    *counter = 0;
    for (;;) {
        (*counter)++;
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

#define DSS_DIAG_IDLE_HITCOUNT_ADDRESS 0x21080028U

int main(void) {
    dss_cache_invalidate_all();

    /* Zero every diagnostic address this build might read without first
     * writing it -- a warm SOFTSYSRST chip reset may not actually clear
     * HSRAM (an open question this itself partly tests), so an
     * un-zeroed diagnostic can read back as stale garbage from many
     * iterations ago instead of proving anything about THIS run. */
    *(volatile uint32_t *)DSS_DIAG_IDLE_HITCOUNT_ADDRESS = 0U;
    *(volatile uint32_t *)0x21080038U = 0U; /* dispatchC marker */
    *(volatile uint32_t *)0x2108003CU = 0U; /* dispatchC intNum */
    *(volatile uint32_t *)0x21080040U = 0U; /* NMI_VEC trap marker */
    *(volatile uint32_t *)0x21080044U = 0U; /* RESERVED2_VEC trap marker */
    *(volatile uint32_t *)0x21080048U = 0U; /* RESERVED3_VEC trap marker */
    *(volatile uint32_t *)0x2108004CU = 0U; /* INT14 trampoline-entered marker */
    *(volatile uint32_t *)0x21080050U = 0U; /* INT14 trampoline SP snapshot */
    *(volatile uint32_t *)0x21080054U = 0U; /* Task_enter() SP snapshot */
    *(volatile uint32_t *)0x21080058U = 0U; /* pxCurrentTCB at scheduler start */
    *(volatile uint32_t *)0x2108005CU = 0U; /* pxCurrentTCB->pxTopOfStack at scheduler start */
    *(volatile uint32_t *)0x21080060U = 0U; /* task A handle */
    *(volatile uint32_t *)0x21080064U = 0U; /* task A initial pxTopOfStack */
    *(volatile uint32_t *)0x21080068U = 0U; /* task B handle */
    *(volatile uint32_t *)0x2108006CU = 0U; /* task B initial pxTopOfStack */
    *(volatile uint32_t *)0x21080070U = 0U; /* Task_enter() log index */
    {
        int i;
        for (i = 0; i < 8; i++) {
            *(volatile uint32_t *)(0x21080074U + (uint32_t)i * 4U) = 0U; /* Task_enter() log [tcb,sp]x4 */
        }
    }
    *(volatile uint32_t *)0x21080094U = 0U; /* SP right after boot-stack interrupt enable */

    /* Temporary diagnostic: capture each task's freshly-computed initial
     * stack pointer (pxTopOfStack) right after creation, before it has
     * ever run -- see port_c674.c's dss_debug_peek_stack() and
     * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes. */
    extern uint32_t dss_debug_peek_stack(TaskHandle_t h);
    TaskHandle_t hA = NULL, hB = NULL;
    xTaskCreate(vCounterTaskA, "A", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &hA);
    xTaskCreate(vCounterTaskB, "B", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &hB);
    *(volatile uint32_t *)0x21080060U = (uint32_t)hA;
    *(volatile uint32_t *)0x21080064U = (hA != NULL) ? dss_debug_peek_stack(hA) : 0xDEADDEADU;
    *(volatile uint32_t *)0x21080068U = (uint32_t)hB;
    *(volatile uint32_t *)0x2108006CU = (hB != NULL) ? dss_debug_peek_stack(hB) : 0xDEADDEADU;

    vTaskStartScheduler();

    /* Only reached if vTaskStartScheduler() itself failed. */
    for (;;) {
    }
}
