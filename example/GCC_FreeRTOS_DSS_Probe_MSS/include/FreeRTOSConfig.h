#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * FreeRTOSConfig.h for the AWR6843 MSS (Cortex-R4F), GCC toolchain,
 * FreeRTOS-Kernel's portable/GCC/ARM_CRx_No_GIC port.
 *
 * This is a from-scratch config for a device this port has never targeted
 * before -- there's no TI-shipped FreeRTOS port for this Hercules-class
 * R4F (the mmWave SDK only offers SYS/BIOS). The tick-timer/interrupt-
 * controller glue this config points at (vConfigureTickInterrupt()/
 * vClearTickInterrupt(), targeting the VIM + RTI peripherals directly)
 * lives in this toolchain repo's shared src/freertos_iwr68xx.c, not this
 * project's own src/ -- see that file's header for the full sourcing and
 * why it's shared rather than local. CONFIRMED WORKING on real hardware
 * (see the AWR6xxx_Toolchain memory notes, gcc_freertos_mss_port.md, for
 * the full bring-up story -- it took real debugging to get here, so don't
 * assume any *future* change to this config or the glue code is safe
 * without re-testing on a board).
 */

#include <stdint.h>

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      ( ( uint32_t ) 200000000 ) /* MSS VCLK -- see MSS_SYS_VCLK in main.c */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 8 )
/* During bring-up (see gcc_freertos_mss_port.md in the AWR6xxx_Toolchain
 * memory notes for the full story), a "flashed but silent" failure was
 * narrowed down to something specific to vTaskDelay()'s deeper call chain
 * (ready-list removal + sorted delayed-list insertion) vs. shallower paths
 * (plain taskYIELD(), vTaskSuspendAll()/xTaskResumeAll() alone) that
 * worked fine -- a pattern consistent with a stack overflow, so the stack
 * size was bumped 512->2048 words (2KB->8KB) per task as a diagnostic
 * step (DATA_RAM is 192KB total, plenty of headroom either way) and
 * vApplicationStackOverflowHook()/vApplicationMallocFailedHook()
 * (src/freertos_glue.c) were given distinct UART-reported failure
 * messages instead of silently hanging. Neither hook ever fired: the
 * larger stack wasn't actually the bug, but it's kept anyway (harmless,
 * more headroom). The real cause turned out to be unrelated -- an
 * RTIA/RTIB register-address mixup in what's now src/freertos_iwr68xx.c,
 * found via a Data-Abort UART report (DFAR/DFSR), not a stack issue at
 * all. */
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 2048 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 64 * 1024 ) )
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

/* Hooks. */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1

/* Software timers. */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE

/* This part's FPU (VFPv3-D16) has 16 D-registers, not 32 -- see
 * GCC-R4F-toolchain.cmake's -mfpu=vfpv3-d16. portASM.S's context save/
 * restore reads this to decide whether to also push/pop D16-D31. */
#define configFPU_D32                           0

/* Optional API inclusions. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  0

/*-----------------------------------------------------------
 * ARM_CRx_No_GIC port glue: tick timer (RTI Compare0) + VIM.
 * See ../../src/freertos_iwr68xx.c (shared toolchain-repo source, not
 * local to this project) for the implementations.
 *---------------------------------------------------------*/
extern void vConfigureTickInterrupt( void );
extern void vClearTickInterrupt( void );
#define configSETUP_TICK_INTERRUPT()    vConfigureTickInterrupt()
#define configCLEAR_TICK_INTERRUPT()    vClearTickInterrupt()

/* portASM.S writes an arbitrary value to this address at the end of every
 * IRQ (a GIC-EOI-shaped step this generic port always performs). VIM has
 * no such "any value" EOI register -- its ack (Hwi_vim.INTREQ, see
 * AWR6843_VIM.h) needs the exact channel bit, not an arbitrary value, so
 * that real ack is done explicitly in vApplicationIRQHandler()
 * (../../src/freertos_iwr68xx.c) instead. This just needs to point at
 * *some* writable word so portASM.S's unconditional write is harmless. */
extern uint32_t g_freertos_dummy_eoi_reg;
#define configEOI_ADDRESS    ( ( uint32_t ) &g_freertos_dummy_eoi_reg )

/* Highest/lowest usable priority values referenced by portmacro.h.  This
 * port's VIM channels aren't a priority-graded scheme the way a GIC's are
 * (each channel is just an independent enable bit) -- these two are only
 * consumed by portLOWEST_INTERRUPT_PRIORITY, which nothing in
 * freertos_iwr68xx.c uses (only one interrupt source, RTI Compare0, is
 * ever configured). Kept at a conservative placeholder. */
#define configUNIQUE_INTERRUPT_PRIORITIES       16

/* A silent taskDISABLE_INTERRUPTS()+infinite-loop here would be
 * indistinguishable from a stack overflow, malloc failure, or any other
 * hang -- tasks.c/list.c/queue.c have many configASSERT() call sites
 * (e.g. the pxTopOfStack alignment check in prvInitialiseNewTask()), any
 * one of which could be silently tripping. vDiagAssertFail()
 * (src/freertos_glue.c) reports "configASSERT FAILED" over UART (plus a
 * fallback LED blink) instead. */
extern void vDiagAssertFail( void );
#define configASSERT( x )                                                                                                                                  \
    if( ( x ) == 0 )                                                                                                                                       \
    {                                                                                                                                                      \
        taskDISABLE_INTERRUPTS();                                                                                                                          \
        vDiagAssertFail();                                                                                                                                 \
    }

#endif /* FREERTOS_CONFIG_H */
