/*
 * cli_vitalsigns.h -- command-line interface task (see cli_vitalsigns.c
 * for the full picture: which commands exist and why others were cut).
 */
#ifndef CLI_VITALSIGNS_H
#define CLI_VITALSIGNS_H

/*! @brief FreeRTOS task entry point: reads lines from the command UART,
 *         parses them, and forwards demo configuration to DSS over the
 *         mailbox. Never returns. */
void vCliTask(void* pvParameters);

#endif /* CLI_VITALSIGNS_H */
