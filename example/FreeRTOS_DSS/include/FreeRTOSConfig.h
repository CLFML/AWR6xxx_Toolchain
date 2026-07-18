/*
 * FreeRTOSConfig.h -- minimal phase-1 bring-up config for FreeRTOS_DSS
 * (proving the port layer itself: scheduler + tick + task switch), not
 * yet dss_main.c's full application. configCPU_CLOCK_HZ matches DSS's
 * 200MHz VCLK (same value example/TI_RTOS_DSS's dss_main.c uses as
 * DSS_SYS_VCLK) -- dss_tick_timer.c's period calculation assumes this.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                   1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                0
#define configCPU_CLOCK_HZ                     200000000UL
#define configTICK_RATE_HZ                     1000
#define configMAX_PRIORITIES                   5
/* Back at the original 512 -- CONFIRMED via a direct hardware SP-capture
 * (a diagnostic stamped B15 into HSRAM right as INT14 was taken) that the
 * earlier bump to 2048 was the wrong diagnosis and made things actively
 * worse, not better: at 2048 words (8192 bytes), a SINGLE task's stack
 * request alone consumes the entire 8KB configTOTAL_HEAP_SIZE (itself
 * capped there by a separate SBR-relocation-field constraint, see that
 * define's own comment) -- xTaskCreate's pvPortMallocStack() call for the
 * counter tasks silently failed (FreeRTOS's own xTaskCreate has NO
 * configASSERT on this path; prvInitialiseNewTask() is just never called,
 * task creation silently no-ops, see tasks.c), leaving the scheduler
 * running with no valid current task -- SP was captured as literally 0x0
 * at the first interrupt, explaining the CPU hanging forever on the
 * dispatcher's very first stack-relative store (an unmapped address 0x0
 * write, not a clean fault on this silicon -- matches the class of
 * unmapped-access behavior noted in swrz099b.pdf's MSS#36 errata) well
 * before ever reaching Hwi_dispatchC. 512 words (2KB/task) comfortably
 * covers the dispatcher's own ~264-byte frame plus normal C call depth,
 * while leaving enough of the 8KB heap for multiple tasks' TCB+stack
 * allocations at once -- see dss_freertos_port.md in the AWR6xxx_Toolchain
 * memory notes for the full empirical trail. */
#define configMINIMAL_STACK_SIZE               512
#define configMAX_TASK_NAME_LEN                16
#define configUSE_16_BIT_TICKS                 0
#define configIDLE_SHOULD_YIELD                1
#define configUSE_MUTEXES                      1
#define configUSE_RECURSIVE_MUTEXES            0
#define configUSE_COUNTING_SEMAPHORES          1
#define configQUEUE_REGISTRY_SIZE              0
#define configUSE_QUEUE_SETS                   0
#define configUSE_TIME_SLICING                 1
#define configUSE_NEWLIB_REENTRANT             0
#define configENABLE_BACKWARD_COMPATIBILITY    0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define configSUPPORT_STATIC_ALLOCATION        1
#define configSUPPORT_DYNAMIC_ALLOCATION       1
/* 8KB, not 32KB: heap_4.c's own statics (xFreeBytesRemaining etc.) are
 * addressed near this heap array via TI's SBR (static-base-relative)
 * relocation, a 15-bit UNSIGNED field (max encodable offset 0x7FFF =
 * 32767 bytes) -- a 32KB heap plus this phase-1 bring-up's own small
 * additions overflowed that field ("relocation ... overflowed", "output
 * file cannot be loaded and run on a target system" -- a real, fatal
 * linker warning, not cosmetic). Two small tasks + idle + timer task
 * don't need anywhere near 32KB anyway. */
#define configTOTAL_HEAP_SIZE                  (8 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configUSE_IDLE_HOOK                    1
#define configUSE_TICK_HOOK                    0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* Back to 1 (the original phase-1 setting): temporarily disabling this
 * was a diagnostic isolation test for the SP=0 dispatcher-hang mystery
 * (see dss_tick_timer.c's DSS_IER_NMIE_BIT comment) -- it came back
 * negative (removing the timer-service task entirely did NOT fix the
 * hang; task B hit the exact same SP=0x0 corruption running first and
 * alone), so there's no reason to keep this subsystem disabled. */
#define configUSE_TIMERS                       1
#define configTIMER_TASK_PRIORITY              (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH               10
#define configTIMER_TASK_STACK_DEPTH           configMINIMAL_STACK_SIZE

#define configUSE_TRACE_FACILITY               0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) {} }

/* Set the following to 1 to include the API function, or zero to exclude
 * the API function. */
#define INCLUDE_vTaskPrioritySet          1
#define INCLUDE_uxTaskPriorityGet         1
#define INCLUDE_vTaskDelete               1
#define INCLUDE_vTaskSuspend              1
#define INCLUDE_vTaskDelayUntil           1
#define INCLUDE_vTaskDelay                1
#define INCLUDE_xTaskGetSchedulerState    1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle    0
#define INCLUDE_eTaskGetState             1
#define INCLUDE_xTimerPendFunctionCall    0
#define INCLUDE_xTaskAbortDelay           0
#define INCLUDE_xTaskGetHandle            0
#define INCLUDE_xSemaphoreGetMutexHolder  0

#endif /* FREERTOS_CONFIG_H */
