#ifndef AWR6843_ESM_H
#define AWR6843_ESM_H
#include "AWR6843_types.h"

/*
 * Layout mirrors TI's mmwave_sdk reg_esm.h (ESMRegs) exactly -- offsets
 * 0x000..0x098, one reserved block (0x05C..0x07F) between group1/2/4 and
 * group7 registers. Plain RwReg fields (not bitfield unions like
 * AWR6843_GIO.h): esm_iwr68xx.c only ever reads/writes whole registers.
 */
typedef struct
{
    RwReg ESMIEPSR1; /* Offset = 0x000 */
    RwReg ESMIEPCR1; /* Offset = 0x004 */
    RwReg ESMIESR1;  /* Offset = 0x008 */
    RwReg ESMIECR1;  /* Offset = 0x00C */
    RwReg ESMILSR1;  /* Offset = 0x010 */
    RwReg ESMILCR1;  /* Offset = 0x014 */
    RwReg ESMSR1;    /* Offset = 0x018 */
    RwReg ESMSR2;    /* Offset = 0x01C */
    RwReg ESMSR3;    /* Offset = 0x020 */
    RwReg ESMEPSR;   /* Offset = 0x024 */
    RwReg ESMIOFFHR; /* Offset = 0x028 */
    RwReg ESMIOFFLR; /* Offset = 0x02C */
    RwReg ESMLTCR;   /* Offset = 0x030 */
    RwReg ESMLTCPR;  /* Offset = 0x034 */
    RwReg ESMEKR;    /* Offset = 0x038 */
    RwReg ESMSSR2;   /* Offset = 0x03C */
    RwReg ESMIEPSR4; /* Offset = 0x040 */
    RwReg ESMIEPCR4; /* Offset = 0x044 */
    RwReg ESMIESR4;  /* Offset = 0x048 */
    RwReg ESMIECR4;  /* Offset = 0x04C */
    RwReg ESMILSR4;  /* Offset = 0x050 */
    RwReg ESMILCR4;  /* Offset = 0x054 */
    RwReg ESMSR4;    /* Offset = 0x058 */
    RoReg RESERVED0[9]; /* Offset = 0x05C .. 0x07F */
    RwReg ESMIEPSR7; /* Offset = 0x080 */
    RwReg ESMIEPCR7; /* Offset = 0x084 */
    RwReg ESMIESR7;  /* Offset = 0x088 */
    RwReg ESMIECR7;  /* Offset = 0x08C */
    RwReg ESMILSR7;  /* Offset = 0x090 */
    RwReg ESMILCR7;  /* Offset = 0x094 */
    RwReg ESMSR7;    /* Offset = 0x098 */
} ESM_Type;

#endif /* AWR6843_ESM_H */
