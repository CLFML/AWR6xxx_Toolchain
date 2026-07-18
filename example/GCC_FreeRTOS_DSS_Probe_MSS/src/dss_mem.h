/*
 * dss_mem.h -- translates a DSS-side address (the same addresses DSS's own
 * linker script/memory map use, e.g. linker_awr6843_dss.cmd in
 * example/TI_RTOS_DSS: L2SRAM_UMAP1 at 0x007E0000, L3SRAM at 0x20000000)
 * into the corresponding MSS-visible address, so MSS can read/write DSS's
 * program/data memory directly with plain pointer accesses -- no DMA, no
 * mailbox protocol, no DSS-side cooperation needed.
 *
 * The ranges below are the mmWave SDK's own address-translation LUT
 * (ti/drivers/soc/platform/soc_xwr68xx.c, SOC_TranslateAddr_LUT[], the
 * table SOC_translateAddress() walks) -- ground truth, not guessed:
 *
 *   { MSS, EDMA, DSS, Size }
 *   { 0x51000000, 0x20000000, 0x20000000, 0x02FFFFFF }  L3 memory / HSRAM
 *   { 0x577E0000, 0x107E0000, 0x007E0000, 0x0081FFFF }  DSS L1/L2 memory
 *   { 0x50000000, 0x06000000, 0x02000000, 0x00FFFFFF }  DSS register space (peripherals)
 *
 * i.e. MSS sees the exact same physical bytes DSS does, just through a
 * different address window -- the same mechanism already used for HSRAM
 * (mmw_messages.h's MMWDEMO_DSS_ALIVE_CHECK_ADDRESS_MSS/_DSS), generalized
 * here to cover all of DSS's L2/L3 memory and DSS-local peripherals
 * (e.g. example/FreeRTOS_DSS's RTI tick timer at DSS address 0x02020000)
 * instead of one fixed address.
 */
#ifndef DSS_MEM_H
#define DSS_MEM_H

#include <stdint.h>
#include <stdbool.h>

/* Returns true and fills *mssAddr on success; false if dssAddr falls
 * outside all known ranges (DSS TCM is NOT covered -- L2/L3 memory and
 * DSS-local peripheral registers are). */
bool dss_mem_translate(uint32_t dssAddr, uint32_t* mssAddr);

/* Opens MSS's own MPU for access to DSS's L2 SRAM alias window
 * (0x577E0000, see dss_mem.c's translate table) -- confirmed necessary on
 * real hardware: the bus aliasing the SDK's SOC_translateAddress() LUT
 * documents exists regardless of MPU config, but MSS's own CPU still
 * data-aborted (DFAR=0x577E0000) writing there before this was added,
 * since Universal_hal's mpu_config() (ported from the mmWave SDK's own
 * SOC_mpu_config(), which never needed MSS to touch DSS's L2 directly)
 * has no region open for it.
 *
 * This Cortex-R4F implements exactly 12 MPU regions (confirmed via CP15's
 * MPU Type Register, see the CLI's mpuInfo command) and mpu_config()
 * already uses all 12 -- there is no free region to add a 13th. Call this
 * ONCE, after soc_init(), to repurpose region index 10 (mpu_config()'s
 * "ADCBuf + Chirp + FFTC + DSS peripheral region", 512KB @ 0x52000000):
 * this debug-probe project has no use for ADC buffer/chirp/FFTC access,
 * unlike a real radar signal-chain example, making it the one region here
 * safe to sacrifice. Deliberately done from THIS project's own code
 * (reaching into Universal_hal's low-level uhal_mpu_* primitives directly,
 * not by editing mpu_config() itself) so no other example sharing
 * Universal_hal is affected. */
void dss_mem_mpu_open_l2(void);

#endif /* DSS_MEM_H */
