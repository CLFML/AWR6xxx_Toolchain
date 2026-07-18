/*
 * linker_freertos_dss.cmd -- adapted from example/TI_RTOS_DSS's
 * linker_awr6843_dss.cmd (itself the reference vital-signs demo's
 * xwr6843_C674x.cmd, trimmed -- see that file's own header). Same MEMORY
 * map (this is the same physical chip), minus the L3RAM size macros tied
 * to mmWave-SDK-specific build defines this project doesn't use (hardcoded
 * to the same physical DSS L3 SRAM size instead, see MMWAVE_L3RAM_SIZE's
 * value in that project's CMakeLists.txt: 6 banks * 0x20000 = 0xC0000),
 * and minus the systemHeap/.l2data/.demoSharedMem sections that project's
 * dss_main.c needs but this from-scratch bring-up doesn't (yet).
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
