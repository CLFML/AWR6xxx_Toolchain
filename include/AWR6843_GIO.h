#ifndef AWR6843_GIO_H
#define AWR6843_GIO_H

#include "AWR6843_types.h"

#define GPIO_MAX_PORT 6U

typedef union
{
    volatile struct
    {
        RwReg RESET : 1; /*<! bit: 0 GIO RESET */
        RwReg NU0 : 31;  /*<! bit: 1..31 NU0 */
    } bit;
    RwReg reg;
} GIO_GIOGCR_Type;

typedef union
{
    volatile struct
    {
        RwReg PWDN : 1; /*<! bit: 0 GIOPWDN Writing to the GIOPWDN bit is only allowed in privilege mode.*/
        RoReg : 31;     /*<! bit: 1..31 RESERVED */
    } bit;
    RwReg reg;
} GIO_GIOPWDN_Type;

typedef union
{
    volatile struct
    {
        RwReg INTDET0 : 8; /*<! bit: 0..7   Interrupt detection select for pins GIOA[7:0] */
        RwReg INTDET1 : 8; /*<! bit: 8..15  Interrupt detection select for pins GIOB[7:0]. */
        RwReg INTDET2 : 8; /*<! bit: 16..23 Interrupt detection select for pins GIOC[7:0]. */
        RwReg INTDET3 : 8; /*<! bit: 24..31 Interrupt detection select for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOINTDET_Type;

typedef union
{
    volatile struct
    {
        RwReg POL0 : 8; /*<! bit: 0..7   Interrupt polarity select for pins GIOA[7:0] */
        RwReg POL1 : 8; /*<! bit: 8..15  Interrupt polarity select for pins GIOOB[7:0]. */
        RwReg POL2 : 8; /*<! bit: 16..23 Interrupt polarity select for pins GIOC[7:0]. */
        RwReg POL3 : 8; /*<! bit: 24..31 Interrupt polarity select for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOPOL_Type;

typedef union
{
    volatile struct
    {
        RwReg ENASET0 : 8; /*<! bit: 0..7   Interrupt enable for pins GIOA[7:0] */
        RwReg ENASET1 : 8; /*<! bit: 8..15  Interrupt enable for pins GIOB[7:0]. */
        RwReg ENASET2 : 8; /*<! bit: 16..23 Interrupt enable for pins GIOC[7:0]. */
        RwReg ENASET3 : 8; /*<! bit: 24..31 Interrupt enable for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOENASET_Type;

typedef union
{
    volatile struct
    {
        RwReg ENACLR0 : 8; /*<! bit: 0..7   Interrupt disable for pins GIOA[7:0] */
        RwReg ENACLR1 : 8; /*<! bit: 8..15  Interrupt disable for pins GIOB[7:0]. */
        RwReg ENACLR2 : 8; /*<! bit: 16..23 Interrupt disable for pins GIOC[7:0]. */
        RwReg ENACLR3 : 8; /*<! bit: 24..31 Interrupt disable for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOENACLR_Type;

typedef union
{
    volatile struct
    {
        RwReg LVLSET0 : 8; /*<! bit: 0..7   GIO high priority interrupt for pins GIOA[7:0] */
        RwReg LVLSET1 : 8; /*<! bit: 8..15  GIO high priority interrupt for pins GIOB[7:0]. */
        RwReg LVLSET2 : 8; /*<! bit: 16..23 GIO high priority interrupt for pins GIOC[7:0]. */
        RwReg LVLSET3 : 8; /*<! bit: 24..31 GIO high priority interrupt for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOLVLSET_Type;

typedef union
{
    volatile struct
    {
        RwReg LVLCLR0 : 8; /*<! bit: 0..7   GIO low priority interrupt for pins GIOA[7:0] */
        RwReg LVLCLR1 : 8; /*<! bit: 8..15  GIO low priority interrupt for pins GIOB[7:0]. */
        RwReg LVLCLR2 : 8; /*<! bit: 16..23 GIO low priority interrupt for pins GIOC[7:0]. */
        RwReg LVLCLR3 : 8; /*<! bit: 24..31 GIO low priority interrupt for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOLVLCLR_Type;

typedef union
{
    volatile struct
    {
        RwReg FLG0 : 8; /*<! bit: 0..7   GIO flag for pins GIOA[7:0] */
        RwReg FLG1 : 8; /*<! bit: 8..15  GIO flag for pins GIOB[7:0]. */
        RwReg FLG2 : 8; /*<! bit: 16..23 GIO flag for pins GIOC[7:0]. */
        RwReg FLG3 : 8; /*<! bit: 24..31 GIO flag for pins GIOD[7:0].*/
    } bit;
    RwReg reg;
} GIO_GIOFLG_Type;

typedef union
{
    volatile struct
    {
        RwReg OFFA : 6; /*<! bit: 0..5 Index bits for currently pending high-priority interrupt Register A*/
        RoReg : 26;     /*<! bit: 6..31 RESERVED */
    } bit;
    RwReg reg;
} GIO_GIOOFFA_Type;

typedef union
{
    volatile struct
    {
        RwReg OFFB : 6; /*<! bit: 0..5 Index bits for currently pending high-priority interrupt Register B*/
        RoReg : 26;     /*<! bit: 6..31 RESERVED */
    } bit;
    RwReg reg;
} GIO_GIOOFFB_Type;

typedef union
{
    volatile struct
    {
        RwReg EMUA : 6; /*<! bit: 0..5 GIO emulation register A */
        RoReg : 26;     /*<! bit: 6..31 RESERVED */
    } bit;
    RwReg reg;
} GIO_GIOEMUA_Type;

typedef union
{
    volatile struct
    {
        RwReg EMUB : 6; /*<! bit: 0..5 GIO emulation register B */
        RoReg : 26;     /*<! bit: 6..31 RESERVED */
    } bit;
    RwReg reg;
} GIO_GIOEMUB_Type;

typedef volatile struct
{
    union
    {
        volatile struct
        {
            RwReg DIR : 8; /*<! GIO data direction of pins in Port n */
            RoReg : 24;    /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIODIR;

    union
    {
        volatile struct
        {
            RwReg DIN : 8; /*<! GIO data input for pins in port n */
            RoReg : 24;    /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIODIN;

    union
    {
        volatile struct
        {
            RwReg DOUT : 8; /*<! GIO data output for pins in port n */
            RoReg : 24;     /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIODOUT;

    union
    {
        volatile struct
        {
            RwReg DSET : 8; /*<! GIO data set for pins in port n */
            RoReg : 24;     /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIODSET;

    union
    {
        volatile struct
        {
            RwReg DCLR : 8; /*<! GIO data clear for pins in port n */
            RoReg : 24;     /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIODCLR;

    union
    {
        volatile struct
        {
            RwReg PDR : 8; /*<! GIO open drain for pins in port n */
            RoReg : 24;    /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIOPDR;

    union
    {
        volatile struct
        {
            RwReg PULLDIS : 8; /*<! GIO pull disable for pins in port n */
            RoReg : 24;        /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIOPULDIS;

    union
    {
        volatile struct
        {
            RwReg PSL : 8; /*<! GIO pull select for pins in port n */
            RoReg : 24;    /*<! RESERVED */
        } bit;
        RwReg reg;
    } GIOPSL;
} GIOPORT_Type;

typedef union
{
    volatile struct
    {
        RwReg SRC : 8; /*<! bit: 0..7 GIO slew rate control for port n */
        RoReg : 24;    /*<! bit: 8..31 RESERVED */
    } bit;
    RwReg reg;
} GIO_GIOSRC_Type;

typedef volatile struct
{
    GIO_GIOGCR_Type GIOGCR;       /* Offset = 0x000 */
    GIO_GIOPWDN_Type GIOPWDN;     /* Offset = 0x004 */
    GIO_GIOINTDET_Type GIOINTDET; /* Offset = 0x008 */
    GIO_GIOPOL_Type GIOPOL;       /* Offset = 0x00C */
    GIO_GIOENASET_Type GIOENASET; /* Offset = 0x010 */
    GIO_GIOENACLR_Type GIOENACLR; /* Offset = 0x014 */
    GIO_GIOLVLSET_Type GIOLVLSET; /* Offset = 0x018 */
    GIO_GIOLVLCLR_Type GIOLVLCLR; /* Offset = 0x01C */
    GIO_GIOFLG_Type GIOFLG;       /* Offset = 0x020 */
    GIO_GIOOFFA_Type GIOOFFA;     /* Offset = 0x024 */
    GIO_GIOOFFB_Type GIOOFFB;     /* Offset = 0x028 */
    GIO_GIOEMUA_Type GIOEMUA;     /* Offset = 0x02C */
    GIO_GIOEMUB_Type GIOEMUB;     /* Offset = 0x030 */

    GIOPORT_Type GIOPORT[GPIO_MAX_PORT];
    GIO_GIOSRC_Type GIOSRCA; /* Offset = 0x134 */
    GIO_GIOSRC_Type GIOSRCB; /* Offset = 0x138 */
    GIO_GIOSRC_Type GIOSRCC; /* Offset = 0x13C */
    GIO_GIOSRC_Type GIOSRCD; /* Offset = 0x140 */
    GIO_GIOSRC_Type GIOSRCE; /* Offset = 0x144 */
    GIO_GIOSRC_Type GIOSRCF; /* Offset = 0x148 */
    GIO_GIOSRC_Type GIOSRCG; /* Offset = 0x14C */
    GIO_GIOSRC_Type GIOSRCH; /* Offset = 0x150 */

} GIO_Type;

#endif /* AWR6843_GIO_H */
