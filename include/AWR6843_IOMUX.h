#ifndef AWR6843_IOMUX_H
#define AWR6843_IOMUX_H
#include "AWR6843_types.h"

typedef union
{
    struct
    {
        RwReg FUNC_SEL : 4;         /*!< bit: 0..3 Pinmux function select                     */
        RwReg IE_OVERRIDE_CTRL : 1; /*!< bit: 4    Pinmux IE (input enable) override control  */
        RwReg IE_OVERRIDE_BIT : 1;  /*!< bit: 5    Pinmux IE (input enable) override bit      */
        RwReg OE_OVERRIDE_CTRL : 1; /*!< bit: 6    Pinmux OE (output enable) override control */
        RwReg OE_OVERRIDE_BIT : 1;  /*!< bit: 7    Pinmux OE (output enable) override bit     */
        RwReg PI_BIT : 1;           /*!< bit: 8    Pinmux PI bit                              */
        RwReg PUPDSEL : 1;          /*!< bit: 9    Pinmux PUPDSEL bit                         */
        RwReg SC1 : 1;              /*!< bit: 10   Pinmux SC1 bit                             */
        RwReg NU : 20;              /*!< bit: 11..31 Pinmux NU bit                            */
        RoReg : 1;                  /*!< bit 32      RESERVED                                 */
    } bit;
    RwReg reg;
} IOMUX_PAD_CFG_Type;

typedef struct
{
    IOMUX_PAD_CFG_Type PADxx_CFG_REG[49]; /* Offset = 0x000 - 0x0C4*/
    RoReg RESERVED[75];                   /* Offset = 0x0C4        */
    RwReg USERMODEEN;                     /* Offset = 0x1F0        */
    RwReg PADGLBLCFGREG;                  /* Offset = 0x1F4        */
    RwReg IOCFGKICK0;                     /* Offset = 0x1F8        */
    RwReg IOCFGKICK1;                     /* Offset = 0x1FC        */
} IOMUX_Type;

#endif /* AWR6843_IOMUX_H */
