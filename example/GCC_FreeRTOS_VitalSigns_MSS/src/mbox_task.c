/*
 * mbox_task.c -- MSS-side mailbox read loop, ported from a reference
 * vital-signs-tracking demo's MmwDemo_mboxReadTask (mss_main.c). See
 * vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain memory notes for
 * the reference this mimics and what changed.
 *
 * The reference task blocks on a Semaphore posted from a Mailbox RX
 * interrupt callback (SYS/BIOS Hwi, needing Hwi_create() -- the same
 * reason esm_iwr68xx.c is optional in this HAL, see Barebones_MSS's
 * CMakeLists.txt). Universal_hal's mailbox driver has no interrupt-driven
 * variant for the same reason (see hal_mailbox.h's header) -- this task
 * polls mailbox_message_pending() instead, with a short vTaskDelay()
 * between checks so it isn't a tight busy-loop starving lower/equal
 * priority tasks.
 *
 * DETOBJ_READY's TLV .address fields are in DSS's own view of HSRAM;
 * MMWDEMO_DSS_HSRAM_ADDR_TO_MSS() (mmw_messages.h) converts to MSS's view
 * before dereferencing -- this testbench's simplification of the
 * reference's general SOC_translateAddress(), see that macro's comment
 * for why HSRAM-only is sufficient here.
 */
#include <FreeRTOS.h>
#include <task.h>
#include <hal_mailbox.h>
#include <hal_uart.h>
#include <string.h>
#include <mmw_messages.h>
#include "mbox_task.h"

/* Matches the reference's dual-UART split: UARTA (command) carries CLI
 * traffic and human-readable status lines, UARTB (logging) carries the
 * binary detection-output stream -- see cli_vitalsigns.c's header for the
 * UARTA baud/pin choice and this project's main.c for UARTB's. */
#define MMWDEMO_LOGGING_UART UART_PERIPHERAL_MSS_SCIB
#define MMWDEMO_COMMAND_UART UART_PERIPHERAL_MSS_SCIA

static void mbox_report_line(const char* msg) {
    uhal_uart_transmit(MMWDEMO_COMMAND_UART, (const uint8_t*)msg, strlen(msg));
}

static void mbox_send_detobj_shipped(void) {
    MmwDemo_message ack;
    memset(&ack, 0, sizeof(ack));
    ack.type = MMWDEMO_MSS2DSS_DETOBJ_SHIPPED;
    ack.subFrameNum = MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG;
    (void)mailbox_write(MAILBOX_PERIPHERAL_MSS_DSS, (const uint8_t*)&ack, sizeof(ack));
}

/* Ported from the reference's MmwDemo_mboxReadTask DETOBJ_READY case
 * (mss_main.c) -- header, then each TLV's {type,length} plus its raw
 * payload, padded to a MMWDEMO_OUTPUT_MSG_SEGMENT_LEN multiple, then the
 * DETOBJ_SHIPPED ack that lets DSS release/reuse its output buffer. */
static void mbox_relay_detobj_ready(const MmwDemo_message* message) {
    uint32_t totalPacketLen;
    uint32_t itemIdx;
    uint32_t numPaddingBytes;

    uhal_uart_transmit(MMWDEMO_LOGGING_UART, (const uint8_t*)&message->body.detObj.header,
                        sizeof(MmwDemo_output_message_header));
    totalPacketLen = sizeof(MmwDemo_output_message_header);

    for (itemIdx = 0; (itemIdx < message->body.detObj.header.numTLVs) && (itemIdx < MMWDEMO_OUTPUT_MSG_MAX); itemIdx++) {
        const MmwDemo_msgTlv* tlv = &message->body.detObj.tlv[itemIdx];
        MmwDemo_output_message_tl tl;

        tl.type = tlv->type;
        tl.length = tlv->length;
        uhal_uart_transmit(MMWDEMO_LOGGING_UART, (const uint8_t*)&tl, sizeof(tl));
        uhal_uart_transmit(MMWDEMO_LOGGING_UART, (const uint8_t*)MMWDEMO_DSS_HSRAM_ADDR_TO_MSS(tlv->address), tlv->length);
        totalPacketLen += (uint32_t)sizeof(tl) + tlv->length;
    }

    numPaddingBytes = MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - (totalPacketLen & (MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - 1U));
    if (numPaddingBytes < MMWDEMO_OUTPUT_MSG_SEGMENT_LEN) {
        uint8_t padding[MMWDEMO_OUTPUT_MSG_SEGMENT_LEN];
        memset(padding, 0, sizeof(padding));
        uhal_uart_transmit(MMWDEMO_LOGGING_UART, padding, numPaddingBytes);
    }

    mbox_send_detobj_shipped();
}

static void mbox_report_hex32(uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    uint8_t buf[10];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = (uint8_t)hex_digits[(value >> (28 - (i * 4))) & 0xFU];
    }
    uhal_uart_transmit(MMWDEMO_COMMAND_UART, buf, sizeof(buf));
}

/* Alive-check: poll for the magic value DSS writes as the very first thing
 * it does (VITALSIGNS_TESTBENCH_MODE build, see the reference DSS project's
 * dss_main.c) -- a raw memory read, no mailbox doorbell/protocol involved,
 * so this works (or doesn't) regardless of anything else on either side.
 * See MMWDEMO_DSS_ALIVE_CHECK_* in mmw_messages.h for why this exists: no
 * JTAG/CCS debug connection in this workflow to just halt DSS and check its
 * PC directly. 20 retries * 250ms = 5s -- generous for a cold boot, short
 * enough not to make a genuinely-dead DSS feel like a hang. */
static void mbox_dss_alive_check(void) {
    volatile uint32_t* const magicAddr = (volatile uint32_t*)MMWDEMO_DSS_ALIVE_CHECK_ADDRESS_MSS;
    uint32_t attempt;

    for (attempt = 0; attempt < 20U; attempt++) {
        if (*magicAddr == MMWDEMO_DSS_ALIVE_CHECK_MAGIC) {
            mbox_report_line("DSS alive-check: OK, magic=");
            mbox_report_hex32(*magicAddr);
            mbox_report_line("\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    mbox_report_line("DSS alive-check: TIMEOUT, DSS never wrote its magic value (still reads ");
    mbox_report_hex32(*magicAddr);
    mbox_report_line(") -- DSS's CPU may never have started running at all "
                      "(see AWR6843 TRM s5.4.2, DSP power domain)\r\n");
}

void vMboxReadTask(void* pvParameters) {
    MmwDemo_message message;
    (void)pvParameters;

    mbox_dss_alive_check();

    for (;;) {
        if (mailbox_message_pending(MAILBOX_PERIPHERAL_MSS_DSS) != UHAL_STATUS_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        if (mailbox_read(MAILBOX_PERIPHERAL_MSS_DSS, (uint8_t*)&message, sizeof(message)) != UHAL_STATUS_OK) {
            continue;
        }

        switch (message.type) {
            case MMWDEMO_DSS2MSS_DETOBJ_READY:
                mbox_relay_detobj_ready(&message);
                break;
            case MMWDEMO_DSS2MSS_CONFIGDONE:
                mbox_report_line("DSS: config done\r\n");
                break;
            case MMWDEMO_DSS2MSS_STOPDONE:
                mbox_report_line("DSS: stop done\r\n");
                break;
            case MMWDEMO_DSS2MSS_ASSERT_INFO:
                mbox_report_line("DSS Exception: ");
                uhal_uart_transmit(MMWDEMO_COMMAND_UART, (const uint8_t*)message.body.assertInfo.file,
                                    strlen(message.body.assertInfo.file));
                mbox_report_line("\r\n");
                break;
            default:
                /* Unrecognized/not-yet-relevant message type -- ignore rather
                 * than assert, unlike the reference DSS's default case (see
                 * mmw_messages.h's header): MSS silently dropping something
                 * it doesn't understand is a much safer default for a
                 * testbench than crashing on it. */
                break;
        }
    }
}
