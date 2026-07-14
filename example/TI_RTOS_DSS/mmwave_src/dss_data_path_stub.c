/*
 * dss_data_path_stub.c -- local no-op stand-ins for everything dss_main.c's
 * compiled object still references from dss_data_path.c and
 * lib/occupancy_detection/lib/common (occupancy-detection "OD demo" and
 * vital-signs feature code), none of which are part of this build (see
 * ti_rtos_dss_replica.md in the AWR6xxx_Toolchain memory notes -- "Stub
 * them locally" scope decision).
 *
 * This list was NOT derived by reading dss_main.c's call graph by eye --
 * it's the exact `nm6x -u` output on the reference project's own compiled
 * dss_main.oe674 (ground truth: whatever the compiler actually emitted
 * relocations for, after -O3 dead-static-function elimination already
 * dropped everything under VITALSIGNS_TESTBENCH_MODE's runtime-dead
 * branches). Every one of these is reachable only through
 * MmwDemo_dssDataPathInit()/MmwDemo_dssDataPathConfig() and the OD-zone
 * mailbox handlers -- code paths VITALSIGNS_TESTBENCH_MODE's
 * MmwDemo_dssInitTask never calls into, but which the compiler must still
 * emit (and the linker must still resolve) because the enclosing functions
 * have external linkage.
 *
 * _MmwDemo_fastCode_L1PSRAM_copy_table is ordinarily a linker-generated
 * COPY_TABLE built by the .fastCode/.overlay SECTIONS scheme in the
 * reference's xwr6843_C674x.cmd -- removed from linker_awr6843_dss.cmd
 * here (see that file's comment) since it named dss_data_path.oe674 by
 * object filename. An empty table (0 records) makes MmwDemo_copyTable()
 * (dss_main.c's own EDMA-based table-copy helper, still compiled since its
 * only caller MmwDemo_dssDataPathInit has external linkage) a safe no-op.
 */
#include "dss_data_path.h"
#include "oddemo_common.h"
#include <oddemo_heatmap.h>
#include <cpy_tbl.h>

/* -------------------------------------------------------------------- */
/* dss_data_path.c stand-ins                                            */
/* -------------------------------------------------------------------- */

void MmwDemo_dataPathObjInit(MmwDemo_DSS_DataPathObj *obj,
                              MmwDemo_DSS_dataPathContext_t *context,
                              MmwDemo_CliCfg_t *cliCfg,
                              MmwDemo_CliCommonCfg_t *cliCommonCfg,
                              MmwDemo_Cfg *cfg)
{
    (void)obj; (void)context; (void)cliCfg; (void)cliCommonCfg; (void)cfg;
}

void MmwDemo_dataPathInit1Dstate(MmwDemo_DSS_DataPathObj *obj)
{
    (void)obj;
}

int32_t MmwDemo_dataPathInitEdma(MmwDemo_DSS_dataPathContext_t *context)
{
    (void)context;
    return 0;
}

int32_t MmwDemo_dataPathConfigEdma(MmwDemo_DSS_DataPathObj *obj)
{
    (void)obj;
    return 0;
}

void MmwDemo_dataPathConfigFFTs(MmwDemo_DSS_DataPathObj *obj)
{
    (void)obj;
}

uint32_t MmwDemo_pow2roundup(uint32_t x)
{
    (void)x;
    return 0U;
}

void MmwDemo_checkDynamicConfigErrors(MmwDemo_DSS_DataPathObj *obj)
{
    (void)obj;
}

/* Linker-generated in the reference build; an empty table is a no-op copy. */
far COPY_TABLE _MmwDemo_fastCode_L1PSRAM_copy_table = {0};

/* -------------------------------------------------------------------- */
/* lib/occupancy_detection stand-ins                                    */
/* -------------------------------------------------------------------- */

void ODDemo_Feature_init(void)
{
}

void ODDemo_Heatmap_steeringVecGen(ODDemo_DataPathObj *obj)
{
    (void)obj;
}

void ODDemo_Heatmap_scale_heatmap16(float *heatin, uint16_t *heatout)
{
    (void)heatin; (void)heatout;
}

void ODDemo_Heatmap_scale_heatmap8(float *heatin, uint8_t *heatout)
{
    (void)heatin; (void)heatout;
}

void VS_Feature_init(void)
{
}

/* -------------------------------------------------------------------- */
/* Global buffers/state normally defined in dss_data_path.c /            */
/* lib/occupancy_detection -- dss_main.c declares all of these `extern`  */
/* itself (see its own top-of-file declarations), so definitions here    */
/* just need to match those exactly.                                    */
/* -------------------------------------------------------------------- */

uint8_t        gMmwL3[ODDEMO_L3_SIZE];

cplxf_t        oddemo_steeringVec[ODDEMO_STEERINGVEC_L1_BUF_SIZE];
uint32_t       oddemo_scratchPad[ODDEMO_SCRATCH_L1_BUF_SIZE];
cplxf_t        oddemo_invRnMatrix[ODDEMO_INVRNMATRIX_BUFFER];
float          oddemo_rangeAzimuthHeatMap[ODDEMO_ANGLEHEATMAP_BUF_SIZE];
uint16_t       oddemo_shortHeatMap[ODDEMO_ANGLEHEATMAP_BUF_SIZE];
cplxf_t        oddemo_invRnMatrix_VS[ODDEMO_INVRNMATRIX_BUF_SIZE];
float          oddemo_coeffMatrix[ODDEMO_ZONE_PAIR][ODDEMO_MATRIX_SIZE];
float          oddemo_meanVec[ODDEMO_ZONE_PAIR][ODDEMO_MATRIX_ROW_SIZE - 1];
float          oddemo_stdVec[ODDEMO_ZONE_PAIR][ODDEMO_MATRIX_ROW_SIZE - 1];
ODDEMO_Zone    oddemo_zone[ODDEMO_MAX_ZONES];
ODDEMO_Parms   oddemo_parms;
uint16_t       oddemo_num_zones;
uint16_t       oddemo_zone_pairs;
uint16_t       oddemo_row_init;
int8_t         oddemo_decision[ODDEMO_MAX_ZONES];
float          oddemo_scratch_output[72];
float          oddemo_row_noise[ODDEMO_MAX_RANGE];
uint8_t        peak_positions[9];
