/*
 * linker_barebones_dss.cmd -- same MEMORY map as example/FreeRTOS_DSS's
 * linker_freertos_dss.cmd (same physical chip, same DSS L2/L3 layout),
 * for a minimal, scheduler-free tick-interrupt diagnostic. See
 * dss_freertos_port.md in the AWR6xxx_Toolchain memory notes for why
 * this exists: isolating whether the nondeterministic INT14/INT15
 * dispatch hazard found in FreeRTOS_DSS is specific to FreeRTOS's
 * task-switch/critical-section machinery, or a fundamental hardware/
 * timing issue independent of any RTOS at all.
 */

MEMORY
{
PAGE 0:
    L2SRAM_UMAP1:   o = 0x007E0000, l = 0x00020000
    L2SRAM_UMAP0:   o = 0x00800000, l = 0x00020000
    L3SRAM:         o = 0x20000000, l = 0x000C0000
    HWA_RAM :       o = 0x21030000, l = 0x00010000
    HSRAM:          o = 0x21080000, l = 0x8000
}

SECTIONS
{
    /* Hardware reset vector is remapped to this address -- confirmed via
     * example/GCC_FreeRTOS_DSS_Probe_MSS's own hardware validation (see
     * dss_probe_mss.md in the AWR6xxx_Toolchain memory notes). */
    .intvecs: {. = align(32); } > 0x007E0000

    .fardata:  {} >> L2SRAM_UMAP0 | L2SRAM_UMAP1
    .const:    {} >> L2SRAM_UMAP0 | L2SRAM_UMAP1
    .switch:   {} >> L2SRAM_UMAP0 | L2SRAM_UMAP1
    .cio:      {} >> L2SRAM_UMAP0 | L2SRAM_UMAP1
    .data:     {} >> L2SRAM_UMAP0 | L2SRAM_UMAP1

    .stack:    {} > L2SRAM_UMAP0 | L2SRAM_UMAP1
    .cinit:    {} > L2SRAM_UMAP0 | L2SRAM_UMAP1
    .far:      {} > L2SRAM_UMAP0 | L2SRAM_UMAP1

    GROUP
    {
       .rodata
       .bss
       .neardata
    } > L2SRAM_UMAP0 | L2SRAM_UMAP1

    .text: {} >> L2SRAM_UMAP1 | L2SRAM_UMAP0
}

--retain="*(.intvecs)"
--stack_size=0x700
