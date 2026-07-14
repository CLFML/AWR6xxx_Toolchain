/*
 * mmw_output.h -- output packet header + vital-signs TLV payload, trimmed
 * from a reference vital-signs-tracking demo's mmw_output.h (see
 * vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain memory notes for
 * where that reference lives and what was cut). Shared between
 * GCC_FreeRTOS_VitalSigns_MSS and whatever DSS testbench firmware gets
 * built against this same header -- not vendored into Universal_hal, same
 * reasoning as AWR6843.h (see that file's header comment).
 *
 * Only one TLV payload type is defined (VS_OUTPUT_HEART_BREATHING_RATES):
 * the reference app's occupancy-detection (ODDemo) heatmap/decision TLVs
 * are out of scope -- this testbench mimics the vital-signs measurement
 * path only, not the occupancy-detection feature bolted onto the same
 * reference fork. VitalSignsDemo_OutputStats is otherwise byte-identical
 * to the reference (all 32 fields, including the reserved7-14 padding at
 * the end), since a real DSS's algorithm output would fill it the same
 * way regardless of which MSS-side feature set relays it.
 */
#ifndef MMW_OUTPUT_H
#define MMW_OUTPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief Output packet length is a multiple of this value, must be power of 2 */
#define MMWDEMO_OUTPUT_MSG_SEGMENT_LEN 32

typedef enum MmwDemo_output_message_type_e {
    /*! @brief Vital-signs heart/breathing rate measurement (VitalSignsDemo_OutputStats) */
    VS_OUTPUT_HEART_BREATHING_RATES = 1,

    MMWDEMO_OUTPUT_MSG_MAX
} MmwDemo_output_message_type;

/*! @brief Message header for reporting detection information from the data path. */
typedef struct MmwDemo_output_message_header_t {
    /*! @brief Output buffer magic word (sync word), fixed {0x0102,0x0304,0x0506,0x0708} */
    uint16_t magicWord[4];

    /*! @brief Total packet length including header, in bytes */
    uint32_t totalPacketLen;

    /*! @brief Platform type */
    uint32_t platform;

    /*! @brief Frame number */
    uint32_t frameNumber;

    /*! @brief Time in CPU cycles when the message was created */
    uint32_t timeCpuCycles;

    /*! @brief Number of detected objects (unused by the vital-signs path, kept for header-layout parity) */
    uint32_t numDetectedObj;

    /*! @brief Number of TLVs following this header */
    uint32_t numTLVs;
} MmwDemo_output_message_header;

/*! @brief Type/length pair preceding each TLV's raw payload on the wire (UART output stream). */
typedef struct MmwDemo_output_message_tl_t {
    /*! @brief TLV type, see MmwDemo_output_message_type */
    uint32_t type;

    /*! @brief Length in bytes */
    uint32_t length;
} MmwDemo_output_message_tl;

/*! @brief Vital-signs measurement output -- the VS_OUTPUT_HEART_BREATHING_RATES TLV payload. */
typedef struct VitalSignsDemo_OutputStats_t {
    uint16_t rangeBinIndexMax;
    uint16_t rangeBinIndexPhase;
    float maxVal;
    uint32_t processingCyclesOut;
    uint16_t rangeBinStartIndex;
    uint16_t rangeBinEndIndex;
    float unwrapPhasePeak_mm;
    float outputFilterBreathOut;
    float outputFilterHeartOut;
    float heartRateEst_FFT;
    float heartRateEst_FFT_4Hz;
    float heartRateEst_xCorr;
    float heartRateEst_peakCount_filtered;
    float breathingRateEst_FFT;
    float breathingRateEst_xCorr;
    float breathingRateEst_peakCount;
    float confidenceMetricBreathOut;
    float confidenceMetricBreathOut_xCorr;
    float confidenceMetricHeartOut;
    float confidenceMetricHeartOut_4Hz;
    float confidenceMetricHeartOut_xCorr;
    float sumEnergyBreathWfm;
    float sumEnergyHeartWfm;
    float motionDetectedFlag;
    float breathingRateEst_harmonicEnergy;
    float heartRateEst_harmonicEnergy;
    float reserved7;
    float reserved8;
    float reserved9;
    float reserved10;
    float reserved11;
    float reserved12;
    float reserved13;
    float reserved14;
} VitalSignsDemo_OutputStats;

#ifdef __cplusplus
}
#endif

#endif /* MMW_OUTPUT_H */
