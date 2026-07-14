/*
 * mmw_config.h -- config structs carried inside MmwDemo_message (see
 * mmw_messages.h), trimmed from a reference vital-signs-tracking demo's
 * mmw_config.h to the vital-signs-measurement subset this testbench
 * implements (calibDcRangeSig/adcbufCfg/vitalSignsCfg/motionDetection/
 * dvsGuiMonitor) -- see vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain
 * memory notes for what was cut and why.
 *
 * Field names/types/order are copied verbatim from the reference for every
 * struct kept here: these get memcpy'd into a mailbox message and read
 * back out by whatever DSS is on the other end by field offset, so an
 * exact match matters. In particular, MmwDemo_ClutterRemovalCfg is
 * deliberately NOT included here even though it exists in the reference's
 * cli.c command table (clutterRemoval) -- the reference DSS's own
 * mailbox-message switch statement has no case for
 * MMWDEMO_MSS2DSS_CLUTTER_REMOVAL (falls into its default case, which
 * asserts) despite the CLI still offering the command. That's a real bug
 * in the reference fork we're mimicking, not something worth reproducing.
 */
#ifndef MMW_CONFIG_H
#define MMW_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief Max number of range bins DC/antenna-coupling calibration can span (positiveBinIdx - negativeBinIdx + 1). */
#define DC_RANGE_SIGNATURE_COMP_MAX_BIN_SIZE 32

/*! @brief DC (zero) range antenna signature removal configuration. */
typedef struct MmwDemo_CalibDcRangeSigCfg_t {
    /*! @brief enabled flag: 1-enabled 0-disabled */
    uint16_t enabled;

    /*! @brief maximum negative range bin (1D FFT index) to be compensated */
    int16_t negativeBinIdx;

    /*! @brief maximum positive range bin (1D FFT index) to be compensated */
    int16_t positiveBinIdx;

    /*! @brief number of chirps in the averaging phase (must be a power of two) */
    uint16_t numAvgChirps;
} MmwDemo_CalibDcRangeSigCfg;

/*! @brief ADCBUF configuration. */
typedef struct MmwDemo_ADCBufCfg_t {
    /*! @brief ADCBUF out format: 0-Complex, 1-Real */
    uint8_t adcFmt;

    /*! @brief ADCBUF IQ swap selection: 0-I in LSB Q in MSB, 1-Q in LSB I in MSB */
    uint8_t iqSwapSel;

    /*! @brief ADCBUF channel interleave configuration: 0-interleaved, 1-non-interleaved */
    uint8_t chInterleave;

    /*! @brief Chirp threshold configuration used for the ADCBUF buffer */
    uint8_t chirpThreshold;
} MmwDemo_ADCBufCfg;

/*! @brief Vital-signs demo GUI monitor selection -- what gets sent to a GUI/host visualizer. */
typedef struct VitalSignsDemo_GuiMonSel_t {
    /*! @brief Flag that can be set from the GUI */
    uint8_t guiFlag_Param1;

    /*! @brief Flag that can be set from the GUI */
    uint8_t guiFlag_Param2;

    /*! @brief Flag that can be set from the GUI */
    uint8_t guiFlag_ClutterRemoval;

    /*! @brief Flag set when the Refresh button is pressed on the GUI */
    uint8_t guiFlag_Reset;

    uint8_t statsInfo;
} VitalSignsDemo_GuiMonSel;

/*! @brief Vital-signs motion-detection gating configuration. */
typedef struct VitalSignsDemo_MotionDetection_t {
    /*! @brief Flag that can be set from the GUI */
    uint16_t enabled;

    /*! @brief Flag that can be set from the GUI */
    uint16_t blockSize;

    /*! @brief Spectral-method threshold used for vital-signs measurement */
    float threshold;

    uint16_t gainControl;
} VitalSignsDemo_MotionDetection;

/*! @brief Vital-signs core algorithm parameters. */
typedef struct VitalSignsDemo_ParamsCfg_t {
    /*! @brief Start range in meters */
    float startRange_m;

    /*! @brief End range in meters */
    float endRange_m;

    /*! @brief Window length for breathing estimation */
    uint16_t winLen_breathing;

    /*! @brief Window length for heart-rate estimation */
    uint16_t winLen_heartRate;

    /*! @brief Threshold for gain control */
    float rxAntennaProcess;

    /*! @brief Alpha factor for exponential smoothing of the breathing/heart waveforms */
    float alpha_breathingWfm;
    float alpha_heartWfm;

    /*! @brief Scaling factors for the breathing/heart waveforms before the FFT */
    float scale_breathingWfm;
    float scale_heartWfm;
} VitalSignsDemo_ParamsCfg;

#ifdef __cplusplus
}
#endif

#endif /* MMW_CONFIG_H */
