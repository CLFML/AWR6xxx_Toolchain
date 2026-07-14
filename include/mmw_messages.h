/*
 * mmw_messages.h -- MSS<->DSS mailbox message protocol, trimmed from a
 * reference vital-signs-tracking demo's mmw_messages.h to the message
 * types GCC_FreeRTOS_VitalSigns_MSS actually sends/receives. See
 * vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain memory notes for
 * the full scope-trim rationale and what got cut (ODDemo occupancy
 * detection, CFAR/beamforming/LVDS/monitor config, the DSS exception
 * software-interrupt path).
 *
 * Message type values are NOT auto-numbered from a fresh enum -- they're
 * pinned to their exact numeric position in the reference's full enum
 * (comments below cite it), because the currently-flashed
 * prebuilt/placeholder_dss.bin in this and every other example directory
 * IS a real compiled build of that reference demo's DSS side (see
 * mailbox_iwr68xx.c's header in Universal_hal for how that was
 * discovered), and getting these values wrong desyncs message routing
 * against a binary we can't recompile from here.
 *
 * MMWDEMO_DSS2MSS_CONFIGDONE is defined but that reference DSS build
 * never actually sends it (confirmed by grepping its dss_main.c source --
 * it relies on mmWaveLink's own start/stop callback path instead, which
 * this testbench doesn't implement, see cli_vitalsigns.c). Kept in the
 * enum for a future custom DSS testbench to use meaningfully; this
 * testbench's own sensorStart/sensorStop are local MSS-state-only for
 * that reason (see cli_vitalsigns.c's header).
 */
#ifndef MMW_MESSAGES_H
#define MMW_MESSAGES_H

#include <stdint.h>
#include "mmw_config.h"
#include "mmw_output.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MmwDemo_message_type_e {
    /* MSS -> DSS. Values pinned to the reference enum's position (it
     * starts at 0xFEED0001 and increments one per entry; entries this
     * testbench doesn't implement are skipped, not renumbered). */
    MMWDEMO_MSS2DSS_CALIB_DC_RANGE_SIG            = 0xFEED0006U, /* reference position 6 */
    MMWDEMO_MSS2DSS_DETOBJ_SHIPPED                = 0xFEED0007U, /* reference position 7 */
    MMWDEMO_MSS2DSS_ADCBUFCFG                     = 0xFEED0009U, /* reference position 9 */
    MMWDEMO_MSS2DSS_VITALSIGNS_MEASUREMENT_PARAMS = 0xFEED001AU, /* reference position 26 */
    MMWDEMO_MSS2DSS_VITALSIGNS_MOTION_DETECTION   = 0xFEED001BU, /* reference position 27 */
    MMWDEMO_VITALSIGNS_GUIMON_CFG                 = 0xFEED001CU, /* reference position 28 */

    /* DSS -> MSS */
    MMWDEMO_DSS2MSS_CONFIGDONE  = 0xFEED0100U,
    MMWDEMO_DSS2MSS_DETOBJ_READY,
    MMWDEMO_DSS2MSS_STOPDONE,
    MMWDEMO_DSS2MSS_ASSERT_INFO
} MmwDemo_message_type;

/*! @brief TLV descriptor inside a DETOBJ_READY message body: type/length/address of one payload. */
typedef struct MmwDemo_msgTlv_t {
    /*! @brief Payload type, see MmwDemo_output_message_type */
    uint32_t type;

    /*! @brief Length in bytes */
    uint32_t length;

    /*! @brief Address of the payload, in the SENDING core's own address map (see HSRAM translation below) */
    uint32_t address;
} MmwDemo_msgTlv;

/*! @brief Only one TLV per frame in this testbench (VS_OUTPUT_HEART_BREATHING_RATES) -- see mmw_output.h. */
typedef struct MmwDemo_detObjMsg_t {
    MmwDemo_output_message_header header;
    MmwDemo_msgTlv tlv[MMWDEMO_OUTPUT_MSG_MAX];
} MmwDemo_detInfoMsg;

#define MMWDEMO_MAX_FILE_NAME_SIZE 128

/*! @brief DSS assertion info, relayed to MSS's command UART for visibility. */
typedef struct MmwDemo_dssAssertInfoMsg_t {
    char file[MMWDEMO_MAX_FILE_NAME_SIZE];
    uint32_t line;
} MmwDemo_dssAssertInfoMsg;

typedef union MmwDemo_message_body_u {
    MmwDemo_CalibDcRangeSigCfg calibDcRangeSigCfg;
    MmwDemo_ADCBufCfg adcBufCfg;
    MmwDemo_detInfoMsg detObj;
    MmwDemo_dssAssertInfoMsg assertInfo;
    VitalSignsDemo_ParamsCfg vitalSignsParamsCfg;
    VitalSignsDemo_MotionDetection motionDetectionParamsCfg;
    VitalSignsDemo_GuiMonSel vitalSigns_GuiMonSel;
} MmwDemo_message_body;

/*! @brief Advanced (per-subframe) frame config is out of scope for this testbench -- every
 *         message uses this sentinel, broadcasting to all subframes (matches the reference's
 *         own legacy/no-advanced-frame-config behavior). */
#define MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG (-1)

typedef struct MmwDemo_message_t {
    MmwDemo_message_type type;
    int8_t subFrameNum;
    MmwDemo_message_body body;
} MmwDemo_message;

/*
 * HSRAM (Hand-Shake RAM): 32KB physically shared between MSS and DSS, but
 * mapped at a different base address in each core's own address space --
 * a real DSS's DETOBJ_READY message points its TLV .address fields at
 * HSRAM using ITS OWN (DSS-side) view of that address, since it's the one
 * writing the payload there. MSS has to translate that to its own view
 * before dereferencing it. This fixed offset (both bases and the region
 * size are compile-time constants from the mmWave SDK's
 * sys_common_xwr68xx_{mss,dss}.h) is a deliberate simplification of the
 * reference SDK's general SOC_translateAddress() (a small lookup table
 * covering L2/L3/HSRAM/etc, since the reference app also points some TLVs
 * directly at DSS L2/L3): this testbench only ever needs the HSRAM entry,
 * so a future DSS testbench built against this header should place any
 * TLV payload it wants MSS to relay in HSRAM specifically, not L2/L3.
 */
#define MMWDEMO_HSRAM_MSS_BASE_ADDRESS 0x52080000U
#define MMWDEMO_HSRAM_DSS_BASE_ADDRESS 0x21080000U
#define MMWDEMO_HSRAM_SIZE             0x8000U

/*! @brief Translate a DSS-side HSRAM address (as found in a received TLV's .address field) to
 *         MSS's own view of the same physical memory. */
#define MMWDEMO_DSS_HSRAM_ADDR_TO_MSS(dssAddr) \
    (((uint32_t)(dssAddr) - MMWDEMO_HSRAM_DSS_BASE_ADDRESS) + MMWDEMO_HSRAM_MSS_BASE_ADDRESS)

/*
 * Alive-check: a raw magic value at HSRAM's very first word, written by DSS
 * (VITALSIGNS_TESTBENCH_MODE build, see dss_main.c's MmwDemo_dssInitTask --
 * the *first* thing it does, before Mailbox_init/anything else) and polled
 * by MSS (see main.c) over plain memory reads -- no mailbox doorbell, no
 * message protocol, nothing that depends on either side's software being
 * further along than "the CPU is executing instructions at all". Exists
 * because this workflow has no JTAG/CCS debug connection to just halt DSS
 * and look at its PC -- this is the only way to distinguish "DSS's CPU
 * never started" (e.g. still power-gated, see the DSP power-domain section
 * of the AWR6843 TRM, swru520e.pdf s5.4.2 -- "On POR, the DSP is powered
 * OFF") from "DSS is running but the mailbox/protocol has a bug".
 */
#define MMWDEMO_DSS_ALIVE_CHECK_ADDRESS_DSS 0x21080000U /* == MMWDEMO_HSRAM_DSS_BASE_ADDRESS */
#define MMWDEMO_DSS_ALIVE_CHECK_ADDRESS_MSS 0x52080000U /* == MMWDEMO_HSRAM_MSS_BASE_ADDRESS */
#define MMWDEMO_DSS_ALIVE_CHECK_MAGIC       0xCAFEF00DU

#ifdef __cplusplus
}
#endif

#endif /* MMW_MESSAGES_H */
