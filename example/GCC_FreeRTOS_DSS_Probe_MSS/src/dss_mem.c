#include <stddef.h>
#include "dss_mem.h"

typedef struct {
    uint32_t mssBase;
    uint32_t dssBase;
    uint32_t size;
} dss_mem_range_t;

/* See dss_mem.h's header for where these two rows come from. */
static const dss_mem_range_t dss_mem_ranges[] = {
    {0x51000000U, 0x20000000U, 0x02FFFFFFU}, /* L3 memory / HSRAM */
    {0x577E0000U, 0x007E0000U, 0x0081FFFFU}, /* DSS L1/L2 memory  */
};
#define DSS_MEM_NUM_RANGES (sizeof(dss_mem_ranges) / sizeof(dss_mem_ranges[0]))

bool dss_mem_translate(uint32_t dssAddr, uint32_t* mssAddr) {
    size_t i;

    for (i = 0; i < DSS_MEM_NUM_RANGES; i++) {
        const dss_mem_range_t* r = &dss_mem_ranges[i];
        if ((dssAddr >= r->dssBase) && (dssAddr <= (r->dssBase + r->size))) {
            *mssAddr = (dssAddr - r->dssBase) + r->mssBase;
            return true;
        }
    }
    return false;
}

/* Same low-level MPU primitives soc_iwr68xx.c's mpu_config() uses
 * (soc_mpu_iwr68xx_gnu.S) -- global symbols in libUniversal_hal.a, callable
 * from here without needing a public Universal_hal header (none exposes
 * them, they're meant as mpu_config()'s own internal building blocks). */
extern void uhal_mpu_disable(void);
extern void uhal_mpu_enable(void);
extern void uhal_mpu_set_region(uint32_t region);
extern void uhal_mpu_set_region_base_address(uint32_t address);
extern void uhal_mpu_set_region_type_and_permission(uint32_t type, uint32_t permission);
extern void uhal_mpu_set_region_size_register(uint32_t value);

/* Same encoding soc_iwr68xx.c's mpu_config() uses (DRSR[5:1] size field,
 * OR with bit0 to enable) -- redefined locally rather than exposed via a
 * header since these are that file's own internal constants. Region 10 is
 * currently 512KB (MPU_SIZE_512_KB = 0x12<<1); the DSS L2 window needs
 * 16MB (2^24) to fully contain 0x577E0000's 0x00820000-byte span while
 * staying naturally aligned -- see dss_mem_ranges[] above and this
 * function's own header comment in dss_mem.h. */
#define MPU_TYPE_NORMAL_OINC_NONSHARED 0x0008U
#define MPU_PERM_RW_USER_RW_NOEXEC     0x1300U
#define MPU_SIZE_16_MB                 (0x17U << 1U)
#define MPU_REGION_ENABLE              1U
#define MPU_REGION_DSS_L2              10U
#define MPU_DSS_L2_ALIAS_BASE          0x57000000U /* 16MB-aligned; see dss_mem.h */

void dss_mem_mpu_open_l2(void) {
    uhal_mpu_disable();

    uhal_mpu_set_region(MPU_REGION_DSS_L2);
    uhal_mpu_set_region_base_address(MPU_DSS_L2_ALIAS_BASE);
    uhal_mpu_set_region_type_and_permission(MPU_TYPE_NORMAL_OINC_NONSHARED, MPU_PERM_RW_USER_RW_NOEXEC);
    uhal_mpu_set_region_size_register(MPU_REGION_ENABLE | MPU_SIZE_16_MB);

    uhal_mpu_enable();
}
