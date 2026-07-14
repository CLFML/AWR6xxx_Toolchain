/*----------------------------------------------------------------------------*/
/* linker_awr6843.ld -- GNU ld port of Barebones_MSS's linker_awr6843.cmd     */
/*                                                                            */
/* Same memory map (VECTORS/PROG_RAM/DATA_RAM), same bare-metal boot as      */
/* Barebones_MSS -- see src/startup_awr6843.S's file header for why the      */
/* reset trampoline needs to do what it does. GNU ld syntax differs from     */
/* TI's linker .cmd format (MEMORY{}/SECTIONS{} look similar but aren't the  */
/* same language), so this is a rewrite, not a mechanical port.             */
/*                                                                            */
/* .text/.rodata/.intvecs: no LMA!=VMA split -- the SBL loads their content  */
/* directly to the address given here (this chip has no execute-in-place    */
/* flash for the app; RPRC's "load_addr" per section is exactly this        */
/* single address when VMA==LMA, see cmake/gen_flash_image.py). .bss has no */
/* file content (SHT_NOBITS), so it's the one section startup_awr6843.S has */
/* always had to zero itself.                                               */
/*                                                                            */
/* .data DOES need an explicit LMA!=VMA split + startup-time copy, unlike   */
/* the rest: an earlier revision of this file assumed the SBL could place   */
/* .data's initial values directly at its DATA_RAM (TCM) VMA with no copy   */
/* needed, the same way .text/.rodata do -- that was never actually         */
/* exercised (GCC_MSS/GCC_FreeRTOS_MSS had zero bytes of initialized .data  */
/* until FreeRTOS-Kernel's own globals, e.g. port.c's ulCriticalNesting,    */
/* broke it). .data's VMA stays in DATA_RAM as before; its LMA is now in    */
/* PROG_RAM right after .rodata (a region proven to load correctly), and    */
/* startup_awr6843.S copies from __data_load__ to __data_start__..          */
/* __data_end__ before zeroing .bss. This is the same LMA-copy TI's own     */
/* __TI_auto_init()/.cinit mechanism already does on the armcl side --      */
/* the tell that "no copy needed" should have been suspected earlier.       */
/*----------------------------------------------------------------------------*/

MEMORY
{
    VECTORS  (rx)  : ORIGIN = 0x00000000, LENGTH = 0x00000100
    PROG_RAM (rx)  : ORIGIN = 0x00000100, LENGTH = 0x0003FF00
    DATA_RAM (rwx) : ORIGIN = 0x08000000, LENGTH = 0x00030000
}

ENTRY(_resetEntry)

SECTIONS
{
    .intvecs : {
        KEEP(*(.intvecs))
    } > VECTORS

    .text : {
        *(.text*)
    } > PROG_RAM

    .rodata : {
        *(.rodata*)
    } > PROG_RAM

    .data : {
        . = ALIGN(8);
        __data_start__ = .;
        *(.data*)
        . = ALIGN(8);
        __data_end__ = .;
    } > DATA_RAM AT> PROG_RAM
    __data_load__ = LOADADDR(.data);

    /* NOLOAD: SHT_NOBITS, no file content, zeroed by startup_awr6843.S
     * (see file header above). */
    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    } > DATA_RAM

    /* Top of DATA_RAM: initial SVC/System-mode stack pointer (stack grows
     * down from here). startup_awr6843.S's own small Abort/Undefined/IRQ/
     * FIQ exception stacks are separate .bss-allocated symbols, not part
     * of this region -- see that file. */
    . = ALIGN(8);
    _estack = ORIGIN(DATA_RAM) + LENGTH(DATA_RAM);

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
    }
}
