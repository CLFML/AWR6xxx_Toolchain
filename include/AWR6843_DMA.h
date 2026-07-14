#ifndef AWR6843_DMA_H
#define AWR6843_DMA_H
#include "AWR6843_types.h"

/*
 * Register layout mirrors TI's mmwave_sdk reg_dma.h/reg_dmaram.h (MSS DMA
 * controller -- two independent instances, NOT the C674x/DSP-side EDMA),
 * the proven/shipped register map for this peripheral -- reused verbatim
 * here instead of re-deriving offsets from the TRM by hand. Only the
 * registers needed for a software-triggered, polling-completion driver are
 * named (no HW-request-line triggering, no per-completion-type interrupts,
 * no memory-protection-region setup); the rest of the module is reserved
 * padding.
 */
typedef struct
{
    RwReg GCTRL;             /* Offset = 0x000: global control (reset/enable) */
    RoReg RESERVED1[8];       /* Offset = 0x004 .. 0x020: PEND/FBREG/DMASTAT/HWCHENAS/HWCHENAR, unused (no HW-triggered channels) */
    RwReg SWCHENAS;             /* Offset = 0x024: software channel-enable set (1 = trigger channel N) */
    RoReg RESERVED2;             /* Offset = 0x028 */
    RwReg SWCHENAR;                /* Offset = 0x02C: software channel-enable reset */
    RoReg RESERVED3[25];             /* Offset = 0x030 .. 0x090: priorities/interrupt-enables/req-line assignment, unused */
    RwReg PAR[4];                      /* Offset = 0x094 .. 0x0A0: per-channel port assignment (8 channels * 4 bits per register) */
    RoReg RESERVED4[32];                 /* Offset = 0x0A4 .. 0x120: chaining maps + per-completion-type interrupt enables, unused (polling, no chaining) */
    RwReg FTCFLAG;                         /* Offset = 0x124: frame-transfer-complete flag (per channel bit, write-1-to-clear) */
    RoReg RESERVED5[5];                      /* Offset = 0x128 .. 0x138: LFSFLAG/HBCFLAG, unused */
    RwReg BTCFLAG;                             /* Offset = 0x13C: block-transfer-complete flag (per channel bit, write-1-to-clear) */
} DMA_Type;

/* GCTRL fields -- confirmed against reg_dma.h and the reset/enable sequence
 * in ti/drivers/dma/src/dma.c's DMA_open() (write 0x1 to reset, then
 * 0x00010000 to bring up out of reset with DMAEN set). */
#define DMA_GCTRL_DMARES      (1U << 0)
#define DMA_GCTRL_DMAEN       (1U << 16)

/* Default port assignment (PortB, matching the mmWave SDK's
 * XWR68XX_DEFAULT_PORT_ASSIGNMENT in platform/dma_xwr68xx.c) replicated
 * into every channel's 4-bit field (3 bits used, 1 reserved) across all 4
 * PAR registers -- this driver doesn't do per-channel port selection. */
#define DMA_PAR_DEFAULT_ALL_CHANNELS 0x77777777U

/*
 * Per-channel primary control packet, ported from reg_dmaram.h's
 * DMARAMRegs (PRIMARYCONTROLPACKETn) -- 32-byte stride per channel. Lives
 * in a separate "packet RAM" address space (the SOC_XWR68XX_MSS_DMA_n_PKT_
 * base address), not the control-register block above.
 */
typedef struct
{
    RwReg ISADDR;  /* Initial source address */
    RwReg IDADDR;  /* Initial destination address */
    RwReg ITCOUNT; /* [31:16] frame count, [15:0] element count */
    RoReg RESERVED1;
    RwReg CHCTRL;  /* Element sizes / xfer type / addr modes / chaining -- see DMA_CHCTRL_* below */
    RwReg EIOFF;   /* Element index offset (indexed addressing only) */
    RwReg FIOFF;   /* Frame index offset (indexed addressing only) */
    RoReg RESERVED2;
} DMA_ChannelPacket_Type;

#define SOC_XWR68XX_NUM_DMA_CHANNELS_PER_INSTANCE 32U

typedef struct
{
    DMA_ChannelPacket_Type PRIMARYCONTROLPACKET[SOC_XWR68XX_NUM_DMA_CHANNELS_PER_INSTANCE];
} DMARAM_Type;

/* CHCTRL bit layout, ported from ti/drivers/dma/src/dma.c's
 * DMA_setChannelParams(): srcElemSize<<14 | destElemSize<<12 |
 * xferType<<8 | srcAddrMode<<3 | destAddrMode<<1 | autoInitiation. */
#define DMA_CHCTRL_SRC_ELEM_SIZE_SHIFT  14U
#define DMA_CHCTRL_DEST_ELEM_SIZE_SHIFT 12U
#define DMA_CHCTRL_XFER_TYPE_SHIFT      8U
#define DMA_CHCTRL_SRC_ADDR_MODE_SHIFT  3U
#define DMA_CHCTRL_DEST_ADDR_MODE_SHIFT 1U
#define DMA_CHCTRL_AUTO_INIT            (1U << 0)

/* DMA_ElemSize_e values (reg_dma.h / dma.h) */
#define DMA_ELEM_SIZE_8BIT  0U
#define DMA_ELEM_SIZE_16BIT 1U
#define DMA_ELEM_SIZE_32BIT 2U
#define DMA_ELEM_SIZE_64BIT 3U

/* DMA_XferType_e values */
#define DMA_XFER_TYPE_FRAME 0U
#define DMA_XFER_TYPE_BLOCK 1U

/* DMA_AddrMode_e values */
#define DMA_ADDR_MODE_CONSTANT       0U
#define DMA_ADDR_MODE_POST_INCREMENT 1U

#endif /* AWR6843_DMA_H */
