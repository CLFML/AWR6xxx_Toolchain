#ifndef AWR6843_MIBSPI_H
#define AWR6843_MIBSPI_H
#include "AWR6843_types.h"

/*
 * Register layout mirrors TI's mmwave_sdk reg_mibspi.h (MSS_MIBSPIA/B), the
 * proven/shipped register map for this peripheral -- reused verbatim here
 * instead of re-deriving offsets from the TRM by hand. Only the registers
 * needed for classic (non-multibuffered, non-DMA) polling master-mode
 * transfers are named; the rest of the module (transfer groups, DMA
 * control, parity/ECC, buffer RAM) is reserved padding -- this driver never
 * enables MIBSPIE (multibuffered mode), so none of that is touched.
 */
typedef struct
{
    RwReg SPIGCR0;         /* Offset = 0x000 */
    RwReg SPIGCR1;          /* Offset = 0x004 */
    RwReg SPIINT0;           /* Offset = 0x008 */
    RoReg RESERVED1;          /* Offset = 0x00C: SPILVL, unused */
    RwReg SPIFLG;               /* Offset = 0x010 */
    RwReg SPIPC0;                 /* Offset = 0x014: pin function select */
    RoReg RESERVED2[8];             /* Offset = 0x018 .. 0x034: SPIPC1..SPIPC8, unused */
    RoReg RESERVED3;                  /* Offset = 0x038: SPIDAT0, unused (no HW-CS latch needed) */
    RwReg SPIDAT1;                      /* Offset = 0x03C: TX + transfer control */
    RoReg SPIBUF;                         /* Offset = 0x040: RX + status flags */
    RoReg RESERVED4[3];                     /* Offset = 0x044 .. 0x04C: SPIEMU, SPIDELAY, SPIDEF, unused */
    RwReg SPIFMT0;                            /* Offset = 0x050: clock/char-length format 0 */
} MIBSPI_Type;

/* Bit positions confirmed against the mmWave SDK's own reg_mibspi.h
 * (ti/drivers/spi/include/reg_mibspi.h) and the register poke sequence in
 * ti/drivers/spi/src/mibspi_dma.c's MIBSPI_initMaster() -- the authoritative
 * register map and init order for this peripheral. */
#define SPIGCR1_MASTER       (1U << 0)
#define SPIGCR1_CLKMOD       (1U << 1)
#define SPIGCR1_SPIEN        (1U << 24)

#define SPIFLG_BUFINITACTIVE (1U << 24)

#define SPIPC0_CLKFUN        (1U << 9)
#define SPIPC0_SIMOFUN0      (1U << 10)
#define SPIPC0_SOMIFUN0      (1U << 11)

/* SPIDAT1 fields */
#define SPIDAT1_TXDATA_SHIFT  0U
#define SPIDAT1_TXDATA_MASK   0x0000FFFFU
#define SPIDAT1_CSNR_SHIFT    16U
#define SPIDAT1_CSNR_MASK     0x00FF0000U
#define SPIDAT1_DFSEL_SHIFT   24U
#define SPIDAT1_CSHOLD        (1U << 28)
/* No hardware chip-select is used by this driver (CS is a plain GPIO pin,
 * toggled by spi_host_start/end_transaction -- see spi_host_iwr68xx.c for
 * why); CSNR is set to "all deasserted" on every transfer. */
#define SPIDAT1_CSNR_NONE     0xFFU

/* SPIBUF fields */
#define SPIBUF_RXDATA_MASK    0x0000FFFFU
#define SPIBUF_RXEMPTY        (1U << 31)

/* SPIFMT0 fields */
#define SPIFMT0_CHARLEN_SHIFT   0U
#define SPIFMT0_PRESCALE_SHIFT  8U
#define SPIFMT0_PHASE          (1U << 16)
#define SPIFMT0_POLARITY       (1U << 17)

#endif /* AWR6843_MIBSPI_H */
