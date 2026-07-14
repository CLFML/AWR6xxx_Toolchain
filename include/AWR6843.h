/*
 * AWR6843.h -- device register header, shared across every example in this
 * toolchain (Barebones_MSS/GCC_MSS/GCC_FreeRTOS_MSS/...), the same role
 * CMSIS device headers play for other ARM MCU toolchains: this repo's own
 * "chip support" layer, kept separate from Universal_hal (a genuinely
 * cross-platform HAL library -- atmelsam/raspberrypi/ti-iwr68xx -- that
 * has no business owning one specific chip's register map). Universal_hal's
 * own iwr68xx driver source (gpio_iwr68xx.c etc.) still #include <AWR6843.h>
 * same as always; each example's CMakeLists.txt is what points Universal_hal
 * at this shared directory (${CMAKE_CURRENT_LIST_DIR}/../../include), not a
 * private per-example copy anymore -- see the AWR6xxx_Toolchain memory notes
 * for why per-example copies caused a real sync bug once already.
 */
#ifndef AWR6843_H
#define AWR6843_H
#include <stdint.h>
#include "AWR6843_types.h"
#include "AWR6843_IOMUX.h"
#include "AWR6843_GIO.h"
#include "AWR6843_SCI.h"
#include "AWR6843_TOPRCM.h"
#include "AWR6843_ESM.h"
#include "AWR6843_MIBSPI.h"
#include "AWR6843_DMA.h"
#include "AWR6843_MAILBOX.h"
#include "AWR6843_VIM.h"
#include "AWR6843_RTI.h"

#define SOC_XWR68XX_MSS_QSPI_BASE_ADDRESS             0xC0800000U
#define SOC_XWR68XX_MSS_DMA_2_PKT_BASE_ADDRESS        0xFCF81000U
#define SOC_XWR68XX_MSS_DMA_2_CTRL_BASE_ADDRESS       0xFCFFF800U
#define SOC_XWR68XX_MSS_DTHE_BASE_ADDRESS             0xFD000000U
#define SOC_XWR68XX_MSS_SHA_BASE_ADDRESS              0xFD004000U
#define SOC_XWR68XX_MSS_AES_BASE_ADDRESS              0xFD006000U
#define SOC_XWR68XX_MSS_CRC_BASE_ADDRESS              0xFE000000U
#define SOC_XWR68XX_MSS_MIBSPIA_RAM_BASE_ADDRESS      0xFF0E0000U
#define SOC_XWR68XX_MSS_MIBSPIB_RAM_BASE_ADDRESS      0xFF0C0000U
#define SOC_XWR68XX_MSS_MCANB_MEM_BASE_ADDRESS        0xFF1E0000U
#define SOC_XWR68XX_MSS_MCAN_MEM_BASE_ADDRESS         0xFF500000U
#define SOC_XWR68XX_MSS_MCANB_ECC_RAM_BASE_ADDRESS    0xFFF7A400U
#define SOC_XWR68XX_MSS_GIO_BASE_ADDRESS              0xFFF7BC00U
#define SOC_XWR68XX_MSS_MCAN_BASE_ADDRESS             0xFFF7C800U
#define SOC_XWR68XX_MSS_I2C_BASE_ADDRESS              0xFFF7D400U
#define SOC_XWR68XX_MSS_MCANB_BASE_ADDRESS            0xFFF7DC00U
#define SOC_XWR68XX_MSS_SCI_A_BASE_ADDRESS            0xFFF7E500U
#define SOC_XWR68XX_MSS_SCI_B_BASE_ADDRESS            0xFFF7E700U
#define SOC_XWR68XX_MSS_MIBSPIA_BASE_ADDRESS          0xFFF7F400U
#define SOC_XWR68XX_MSS_MIBSPIB_BASE_ADDRESS          0xFFF7F600U
#define SOC_XWR68XX_MSS_GPIO_BASE_ADDRESS             0xFFF7BC00U
#define SOC_XWR68XX_MSS_DMA_1_PKT_BASE_ADDRESS        0xFFF80000U
#define SOC_XWR68XX_MSS_TOP_RCM_BASE_ADDRESS          0xFFFFE100U
#define SOC_XWR68XX_MSS_PINMUX_BASE_ADDRESS           0xFFFFEA00U
/* MSS_RTIA (0xFFFFFC00) is the plain "Real Time Interrupt" module -- the
 * one meant for general-purpose periodic timing (its rti_vclk comes from
 * MSS_RCM's normal peripheral clock domain, same as GPIO/UART/etc.).
 * MSS_RTIB (0xFFFFEE00, "RTI With Digital Watchdog Timer") is a DIFFERENT
 * peripheral -- its wdt_vclk/wdt_sync/wdt_rstn are a separate,
 * watchdog-specific clock/reset domain (TRM SWRU520E figure "Integration
 * of MSS_RTIA and MSS_RTIB"). Confirmed via the actual TRM after
 * accidentally targeting RTIB here first: writing to RTIB's registers
 * before its watchdog clock domain is set up bus-faults (Data Abort,
 * DFAR=0xFFFFEE00, DFSR=synchronous external abort) -- see
 * src/freertos_iwr68xx.c and the AWR6xxx_Toolchain memory notes
 * (gcc_freertos_mss_port.md) for the full story. Both share the same
 * register layout (RTIGCTRL at offset 0, etc., confirmed against the TRM)
 * -- only the base address differs. */
#define SOC_XWR68XX_MSS_RTIA_BASE_ADDRESS             0xFFFFFC00U
#define SOC_XWR68XX_MSS_RTIB_BASE_ADDRESS             0xFFFFEE00U
#define SOC_XWR68XX_MSS_ESM_BASE_ADDRESS              0xFFFFF500U
/* VIM (Vectored Interrupt Manager) line numbers, not memory addresses --
 * kept alongside the base addresses above since they're the other half of
 * "how to wire up the ESM peripheral." */
#define SOC_XWR68XX_MSS_ESM_HIGH_PRIORITY_INT         0U
#define SOC_XWR68XX_MSS_ESM_LOW_PRIORITY_INT          20U
/* VIM channel for RTI Compare0 -- used as the FreeRTOS tick source (see
 * src/freertos_iwr68xx.c). From the mmWave SDK's sys_common_xwr68xx_mss.h
 * (SOC_XWR68XX_MSS_RTI_COMPARE0_INT). */
#define SOC_XWR68XX_MSS_RTI_COMPARE0_INT              2U
#define SOC_XWR68XX_MSS_GPCFG_BASE_ADDRESS            0xFFFFF800U
#define SOC_XWR68XX_MSS_DMA_1_CTRL_BASE_ADDRESS       0xFFFFF000U
#define SOC_XWR68XX_MSS_RCM_BASE_ADDRESS              0xFFFFFF00U
/*Next 4 defines: MSS mailbox base addresses to communicate with BSS*/
#define SOC_XWR68XX_MSS_MBOX_MSS_BSS_REG_BASE_ADDRESS 0xF0608000U
#define SOC_XWR68XX_MSS_MBOX_MSS_BSS_MEM_BASE_ADDRESS 0xF0602000U
#define SOC_XWR68XX_MSS_MBOX_BSS_MSS_REG_BASE_ADDRESS 0xF0608600U
#define SOC_XWR68XX_MSS_MBOX_BSS_MSS_MEM_BASE_ADDRESS 0xF0601000U
/*Next 4 defines: MSS mailbox base addresses to communicate with DSS*/
#define SOC_XWR68XX_MSS_MBOX_MSS_DSS_REG_BASE_ADDRESS 0xF0608400U
#define SOC_XWR68XX_MSS_MBOX_MSS_DSS_MEM_BASE_ADDRESS 0xF0604000U
#define SOC_XWR68XX_MSS_MBOX_DSS_MSS_REG_BASE_ADDRESS 0xF0608300U
#define SOC_XWR68XX_MSS_MBOX_DSS_MSS_MEM_BASE_ADDRESS 0xF0605000U

/*
 * Defined 'static' on purpose: these pointer constants live in a header that is
 * included by multiple translation units (gpio, pinmux, application). A plain
 * global definition here produces duplicate-symbol link errors as soon as more
 * than one .c uses it; 'static const' gives each unit its own private constant
 * pointer, which the compiler folds away to a direct MMIO access.
 */
static volatile IOMUX_Type* const IOMUX = (volatile IOMUX_Type*)SOC_XWR68XX_MSS_PINMUX_BASE_ADDRESS;
static volatile GIO_Type* const GIO = (volatile GIO_Type*)SOC_XWR68XX_MSS_GIO_BASE_ADDRESS;
static volatile SCI_Type* const SCI_A = (volatile SCI_Type*)SOC_XWR68XX_MSS_SCI_A_BASE_ADDRESS;
static volatile SCI_Type* const SCI_B = (volatile SCI_Type*)SOC_XWR68XX_MSS_SCI_B_BASE_ADDRESS;
static volatile TOPRCM_Type* const TOP_RCM = (volatile TOPRCM_Type*)SOC_XWR68XX_MSS_TOP_RCM_BASE_ADDRESS;
static volatile ESM_Type* const ESM = (volatile ESM_Type*)SOC_XWR68XX_MSS_ESM_BASE_ADDRESS;
static volatile MIBSPI_Type* const MIBSPI_A = (volatile MIBSPI_Type*)SOC_XWR68XX_MSS_MIBSPIA_BASE_ADDRESS;
static volatile MIBSPI_Type* const MIBSPI_B = (volatile MIBSPI_Type*)SOC_XWR68XX_MSS_MIBSPIB_BASE_ADDRESS;
static volatile DMA_Type* const DMA_1 = (volatile DMA_Type*)SOC_XWR68XX_MSS_DMA_1_CTRL_BASE_ADDRESS;
static volatile DMA_Type* const DMA_2 = (volatile DMA_Type*)SOC_XWR68XX_MSS_DMA_2_CTRL_BASE_ADDRESS;
static volatile DMARAM_Type* const DMA_1_RAM = (volatile DMARAM_Type*)SOC_XWR68XX_MSS_DMA_1_PKT_BASE_ADDRESS;
static volatile DMARAM_Type* const DMA_2_RAM = (volatile DMARAM_Type*)SOC_XWR68XX_MSS_DMA_2_PKT_BASE_ADDRESS;
static volatile MAILBOX_Type* const MBOX_MSS_BSS_REG = (volatile MAILBOX_Type*)SOC_XWR68XX_MSS_MBOX_MSS_BSS_REG_BASE_ADDRESS;
static volatile MAILBOX_Type* const MBOX_BSS_MSS_REG = (volatile MAILBOX_Type*)SOC_XWR68XX_MSS_MBOX_BSS_MSS_REG_BASE_ADDRESS;
static volatile MAILBOX_Type* const MBOX_MSS_DSS_REG = (volatile MAILBOX_Type*)SOC_XWR68XX_MSS_MBOX_MSS_DSS_REG_BASE_ADDRESS;
static volatile MAILBOX_Type* const MBOX_DSS_MSS_REG = (volatile MAILBOX_Type*)SOC_XWR68XX_MSS_MBOX_DSS_MSS_REG_BASE_ADDRESS;
static volatile VIM_Type* const VIM = (volatile VIM_Type*)SOC_XWR68XX_MSS_VIM_BASE_ADDRESS;
static volatile RTI_Type* const RTI_A = (volatile RTI_Type*)SOC_XWR68XX_MSS_RTIA_BASE_ADDRESS;







#endif /* AWR6843_H */
