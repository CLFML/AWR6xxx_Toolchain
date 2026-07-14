#ifndef AWR6843_MAILBOX_H
#define AWR6843_MAILBOX_H
#include "AWR6843_types.h"

/*
 * Register layout mirrors TI's mmwave_sdk reg_mailbox.h -- the proven/
 * shipped register map for this peripheral, reused verbatim here instead
 * of re-deriving offsets from the TRM by hand.
 *
 * Each MAILBOX register block implements two independent single-bit
 * doorbell lines multiplexed into the same set of registers: bit 0 is the
 * "mailbox" (new-message) line, bit 1 is the "mailbox ack" line. Which
 * physical direction each bit is driven/observed in depends on which of
 * the SoC's four register blocks you're looking at -- see
 * AWR6843.h's SOC_XWR68XX_MSS_MBOX_*_REG_BASE_ADDRESS comments and
 * mailbox_iwr68xx.c's file header for the protocol built on top of this.
 */
typedef struct
{
    RwReg INT_MASK;         /* Offset = 0x000 */
    RoReg RESERVED1;          /* Offset = 0x004 */
    RwReg INT_MASK_SET;         /* Offset = 0x008 */
    RoReg RESERVED2;              /* Offset = 0x00C */
    RwReg INT_MASK_CLR;             /* Offset = 0x010 */
    RoReg RESERVED3;                  /* Offset = 0x014 */
    RwReg INT_STS_CLR;                  /* Offset = 0x018 */
    RoReg RESERVED4;                      /* Offset = 0x01C */
    RwReg INT_ACK;                          /* Offset = 0x020: write 1 to clear/acknowledge a line */
    RoReg RESERVED5;                          /* Offset = 0x024 */
    RwReg INT_TRIG;                              /* Offset = 0x028: write 1 to assert a line to the remote side */
    RoReg RESERVED6;                              /* Offset = 0x02C */
    RoReg INT_STS_MASKED;                          /* Offset = 0x030: masked pending status per line */
    RoReg RESERVED7;                                  /* Offset = 0x034 */
    RoReg INT_STS_RAW;                                  /* Offset = 0x038 */
} MAILBOX_Type;

/* Bit 0 = "mailbox" (new-message) line, bit 1 = "mailbox ack" line --
 * identical bit meaning across INT_MASK/INT_STS_CLR/INT_ACK/INT_TRIG/
 * INT_STS_MASKED/INT_STS_RAW, confirmed against the mmWave SDK's
 * reg_mailbox.h and the register poke sequence in
 * ti/drivers/mailbox/src/mailbox.c (Mailbox_write/Mailbox_read/
 * Mailbox_readFlush and the box-full/box-empty ISR processing functions). */
#define MAILBOX_INT_MAILBOX_BIT     (1U << 0)
#define MAILBOX_INT_MAILBOX_ACK_BIT (1U << 1)

#endif /* AWR6843_MAILBOX_H */
