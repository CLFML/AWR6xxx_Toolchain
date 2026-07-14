#ifndef AWR6843_VIM_H
#define AWR6843_VIM_H
#include "AWR6843_types.h"

/*
 * Register layout and base address ported from TI SYS/BIOS's own VIM driver
 * (ti.sysbios.family.arm.v7r.vim.Hwi -- the R4F Vectored Interrupt Manager
 * module) since the mmWave SDK itself ships no VIM register header (VIM
 * setup is normally entirely SYS/BIOS's job). Base address 0xFFFFFDEC is
 * TI's own "vimBaseAddress" constant, taken verbatim from
 * ti/sysbios/family/arm/v7r/vim/Hwi.xs (bios_6_73_01_01) -- the same
 * package this project already reverse-engineered for the VIM soft-reset
 * (RCM SOFTRST2) and ESM-clear sequence used by Barebones_MSS's boot code.
 * Struct layout ported from Hwi.xdc's "struct VIM" -- only the fields this
 * project's minimal FreeRTOS port needs (FIRQPR/INTREQ/REQENASET, for
 * routing one VIM channel to IRQ and acking it) are named; the rest
 * (WAKEENA*, IRQVECREG/FIQVECREG, CAPEVT, CHANCTRL) is reserved padding
 * for now.
 */
typedef struct
{
    RoReg RESERVED1[9]; /* 0x00: ECCSTAT, ECCCTL, UERRADDR, FBVECADDR, SBERRADDR, IRQINDEX, FIQINDEX, RES00[2] -- unused */
    RwReg FIRQPR[4];     /* 0x24: per-channel FIQ(1)/IRQ(0) select, 32 channels per word */
    RwReg INTREQ[4];      /* 0x34: pending-interrupt ack -- write 1 to a channel's bit to clear it (see Hwi_clearInterrupt()) */
    RwReg REQENASET[4];    /* 0x44: per-channel interrupt enable set (write 1 to enable) */
} VIM_Type;

#define SOC_XWR68XX_MSS_VIM_BASE_ADDRESS 0xFFFFFDECU

#endif /* AWR6843_VIM_H */
