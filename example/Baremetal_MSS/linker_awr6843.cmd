/*----------------------------------------------------------------------------*/
/* XWR68XX.cmd                                                                */
/*                                                                            */
/* (c) Texas Instruments 2016, All rights reserved.                          */
/*                                                                            */

/* USER CODE BEGIN (0) */
/* USER CODE END */


/*----------------------------------------------------------------------------*/
/* Linker Settings                                                            */
--retain="*(.intvecs)"
/* Third bare-metal boot attempt on this board -- see src/startup_awr6843.asm's */
/* file header and the AWR6xxx_Toolchain memory note on xdctools/configuro's    */
/* generated output. Earlier attempts left Abort/Undefined-mode stacks         */
/* uninitialized and never reset VIM/cleared ESM; this one's startup           */
/* trampoline does both before calling main().                                 */

/* .stack is declared as a zero-size .usect in startup_awr6843.asm (the RTS   */
/* boot convention) -- this -stack directive is what actually reserves 0x1000 */
/* bytes for it. Must match __STACK_SIZE's .set value in that file exactly:   */
/* the linker grows .stack to (at least) this size, while the asm file        */
/* computes sp = __stack + __STACK_SIZE independently: two different sources  */
/* for the same number, kept in sync by convention, not by a shared symbol.   */
-stack 0x1000

/*----------------------------------------------------------------------------*/
/* Memory Map                                                                 */
MEMORY{
    VECTORS  (X)  : origin=0x00000000 length=0x00000100
    PROG_RAM (RX) : origin=0x00000100 length=0x0003FF00
    DATA_RAM (RW) : origin=0x08000000 length=0x00030000
    L3_RAM (RW)   : origin=0x51000000 length=0x000A0000
    HS_RAM (RW)   : origin=0x52080000 length=0x8000
}

/*----------------------------------------------------------------------------*/
/* Section Configuration                                                      */
SECTIONS{
    .intvecs : {} > VECTORS
    .text    : {} > PROG_RAM ALIGN(8)
    .const   : {} > PROG_RAM ALIGN(8)
    .cinit   : {} > PROG_RAM ALIGN(8)
    .pinit   : {} > PROG_RAM ALIGN(8)
    .bss     : {} > DATA_RAM
    .data    : {} > DATA_RAM
    .stack   : {} > DATA_RAM ALIGN(32)
}
/*----------------------------------------------------------------------------*/
