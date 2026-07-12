#ifndef AWR6843_SCI_H
#define AWR6843_SCI_H
#include "AWR6843_types.h"

/*
 * Register layout mirrors TI's mmwave_sdk reg_sci.h (MSS_SCIA), which is the
 * proven/shipped register map for this peripheral -- reused verbatim here
 * instead of re-deriving offsets from the TRM by hand.
 */
typedef struct
{
    RwReg SCIGCR0;       /* Offset = 0x000 */
    RwReg SCIGCR1;        /* Offset = 0x004 */
    RoReg RESERVED1;       /* Offset = 0x008 */
    RwReg SCISETINT;        /* Offset = 0x00C */
    RwReg SCICLEARINT;       /* Offset = 0x010 */
    RwReg SCISETINTLVL;       /* Offset = 0x014 */
    RwReg SCICLEARINTLVL;      /* Offset = 0x018 */
    RwReg SCIFLR;               /* Offset = 0x01C */
    RwReg SCIINTVECT0;           /* Offset = 0x020 */
    RwReg SCIINTVECT1;            /* Offset = 0x024 */
    RwReg SCICHAR;                 /* Offset = 0x028 */
    RwReg SCIBAUD;                  /* Offset = 0x02C */
    RoReg SCIED;                     /* Offset = 0x030 */
    RoReg SCIRD;                      /* Offset = 0x034 */
    WoReg SCITD;                       /* Offset = 0x038 */
    RwReg SCIPIO0;                      /* Offset = 0x03C */
} SCI_Type;

/* SCIGCR1 bit positions used by the driver below (see reg_sci.h) */
#define SCIGCR1_TIMING_MODE (1U << 1)
#define SCIGCR1_PARITY_ENA  (1U << 2)
#define SCIGCR1_STOP_2BIT   (1U << 4)
#define SCIGCR1_CLOCK       (1U << 5)
#define SCIGCR1_SW_NRESET   (1U << 7)
#define SCIGCR1_RXENA       (1U << 24)
#define SCIGCR1_TXENA       (1U << 25)

#define SCIFLR_TXRDY_BIT    (1U << 8)

#define SCIPIO0_RX_FUNC     (1U << 1)
#define SCIPIO0_TX_FUNC     (1U << 2)

#endif /* AWR6843_SCI_H */
