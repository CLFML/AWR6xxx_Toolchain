/*
 * mbox_task.h -- FreeRTOS task that owns the MSS<->DSS mailbox link (see
 * mbox_task.c for the full picture).
 */
#ifndef MBOX_TASK_H
#define MBOX_TASK_H

/*! @brief FreeRTOS task entry point: reads MmwDemo_message traffic from
 *         DSS and relays MMWDEMO_DSS2MSS_DETOBJ_READY frames to the
 *         logging UART. Never returns. */
void vMboxReadTask(void* pvParameters);

#endif /* MBOX_TASK_H */
