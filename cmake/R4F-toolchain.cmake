set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSTEM_VERSION 1)

# Setup CMake's rules for using the CMAKE_FIND_ROOT_PATH for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# TI cgtools command line programs
set(SDK_PATH ${TI_CGT_ROOT})
set(SDK_BIN "${SDK_PATH}/bin")

# This compiler doesn't work with CMAKE_SYSROOT, but we do need to set our
# CMAKE_FIND_ROOT_PATH. Note that setting a variable will override the value in
# the cache if the CACHE option was not used, so if we want to be able to use a
# CMAKE_FIND_ROOT_PATH passed to cmake via the command line, we must make sure
# not to overwrite any value that was already set.
if(NOT CMAKE_FIND_ROOT_PATH)
   set(CMAKE_FIND_ROOT_PATH ${SDK_PATH})
endif()

set(CMAKE_C_COMPILER "${SDK_BIN}/armcl")
set(CMAKE_CXX_COMPILER "${SDK_BIN}/armcl")
set(CMAKE_ASM_COMPILER "${SDK_BIN}/armcl")
set(CMAKE_AR "${SDK_BIN}/armar")
set(CMAKE_LINKER "${SDK_BIN}/armlnk")
# set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} --symdebug:dwarf")

add_custom_target(setup_runtime_libraries ALL
    COMMAND ${CMAKE_COMMAND} -E env bash -c "
        if [ ! -f /home/victor/ti/ti-cgt-arm_16.9.6.LTS/lib/rtsv7R4_T_le_v3D16_eabi.lib ]; then \
            PATH=$ENV{PATH}:/home/victor/ti/ti-cgt-arm_16.9.6.LTS/bin \
            /home/victor/ti/ti-cgt-arm_16.9.6.LTS/lib/mklib --pattern rtsv7R4_T_le_v3D16_eabi.lib > /dev/null 2>&1; \
        else \
            echo 'Runtime library was already built'; \
        fi"
    COMMENT "Setting up runtime libraries"
    VERBATIM
)


add_compile_options(
   -mv7R4
   --code_state=16
   --float_support=VFPv3D16
   -me
   -g
   --diag_warning=225
   --diag_wrap=off
   --display_error_number
   --enum_type=int
   --abi=eabi
   --define=SOC_XWR68XX 
   --define=SUBSYS_MSS
)



add_link_options(
   -mv7R4
   -m "mss_program.xer4f.map"
   "${TI_CGT_ROOT}/lib/rtsv7R4_T_le_v3D16_eabi.lib"
   "${TI_CGT_ROOT}/lib/libc.a"
   --heap_size=0x800
   --stack_size=0x800
   --reread_libs
   --warn_sections
   --rom_model
   -c 
   --unused_section_elimination=on
   --compress_dwarf=on
   -l${TI_LINKER_CMD}
)

# Normally, cmake checks if the compiler can compile and link programs. For
# TI's cgtools the link step doesn't work, and there seems to be no easy way
# to fix this. Instead, we simply disable the compiler checks for C and C++.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

include_directories(${TI_CGT_ROOT}/include
   ${TI_CGT_ROOT}/lib/src)


add_compile_options(--float_support=vfpv3d16)


# add_library(GTRACK_LIB STATIC IMPORTED)
# SET_TARGET_PROPERTIES(GTRACK_LIB PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/alg/mmwavelib/lib/)
include_directories(${TI_MMWAVE_PATH}/packages
                    ${TI_MMWAVE_PATH}/packages/ti/common)

add_library(MMWAVE_DRIVER_ADCBUF STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_ADCBUF PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf/lib/libadcbuf_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_ADCBUF INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf/include
                                                          ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf)

add_library(MMWAVE_DRIVER_CAN STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_CAN PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf/lib/libcan_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_CAN INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/can/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/can)

add_library(MMWAVE_DRIVER_CANFD STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_CANFD PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf/lib/libcanfd_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_CANFD INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/canfd/include
                                                         ${TI_MMWAVE_PATH}/packages/ti/drivers/canfd)

add_library(MMWAVE_DRIVER_CBUFF STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_CBUFF PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/cbuff/lib/libcbuff_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_CBUFF INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/cbuff/include
                                                         ${TI_MMWAVE_PATH}/packages/ti/drivers/cbuff)

add_library(MMWAVE_DRIVER_CRC STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_CRC PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/crc/lib/libcrc_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_CRC INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/crc/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/crc)

add_library(MMWAVE_DRIVER_CRYPTO STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_CRYPTO PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/crypto/lib/libcrypto_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_CRYPTO INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/crypto/include
                                                          ${TI_MMWAVE_PATH}/packages/ti/drivers/crypto)

add_library(MMWAVE_DRIVER_CSI STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_CSI PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/csi/lib/libcsi_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_CSI INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/csi/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/csi)

add_library(MMWAVE_DRIVER_DMA STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_DMA PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/dma/lib/libdma_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_DMA INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/dma/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/dma)

add_library(MMWAVE_DRIVER_EDMA STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_EDMA PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/edma/lib/libedma_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_EDMA INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/edma/include
                                                        ${TI_MMWAVE_PATH}/packages/ti/drivers/edma)

add_library(MMWAVE_DRIVER_ESM STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_ESM PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/esm/lib/libesm_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_ESM INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/esm/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/esm)

add_library(MMWAVE_DRIVER_GPIO STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_GPIO PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/gpio/lib/libgpio_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_GPIO INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/gpio/include
                                                        ${TI_MMWAVE_PATH}/packages/ti/drivers/gpio)

add_library(MMWAVE_DRIVER_HWA STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_HWA PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/hwa/lib/libhwa_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_HWA INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/hwa/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/hwa)

add_library(MMWAVE_DRIVER_I2C STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_I2C PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/i2c/lib/libi2c_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_I2C INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/i2c/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/i2c)

add_library(MMWAVE_DRIVER_MAILBOX STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_MAILBOX PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/mailbox/lib/libmailbox_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_MAILBOX INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/mailbox/include
                                                           ${TI_MMWAVE_PATH}/packages/ti/drivers/mailbox)

add_library(MMWAVE_DRIVER_OSAL STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_OSAL PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/osal/lib/libosal_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_OSAL INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/osal)

add_library(MMWAVE_DRIVER_PINMUX STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_PINMUX PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/pinmux/lib/libpinmux_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_PINMUX INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/pinmux/include
                                                          ${TI_MMWAVE_PATH}/packages/ti/drivers/pinmux)

add_library(MMWAVE_DRIVER_QSPI STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_QSPI PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/qspi/lib/libqspi_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_QSPI INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/qspi/include
                                                        ${TI_MMWAVE_PATH}/packages/ti/drivers/qspi)

add_library(MMWAVE_DRIVER_QSPIFLASH STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_QSPIFLASH PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/qspiflash/lib/libqspiflash_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_QSPIFLASH INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/qspiflash/include
                                                             ${TI_MMWAVE_PATH}/packages/ti/drivers/qspiflash)

add_library(MMWAVE_DRIVER_SOC STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_SOC PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/soc/lib/libsoc_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_SOC INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/soc/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/soc)

add_library(MMWAVE_DRIVER_SPI STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_SPI PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/spi/lib/libspi_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_SPI INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/spi/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/spi)

add_library(MMWAVE_DRIVER_UART STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_UART PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/uart/lib/libuart_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_UART INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/uart/include
                                                        ${TI_MMWAVE_PATH}/packages/ti/drivers/uart)

add_library(MMWAVE_DRIVER_WATCHDOG STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_DRIVER_WATCHDOG PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/watchdog/lib/libwatchdog_xwr68xx.aer4f)
target_include_directories(MMWAVE_DRIVER_WATCHDOG INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/watchdog/include
                                                            ${TI_MMWAVE_PATH}/packages/ti/drivers/watchdog)

add_library(CONTROL_MMWAVELINK STATIC IMPORTED)
SET_TARGET_PROPERTIES(CONTROL_MMWAVELINK PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/control/mmwavelink/lib/libmmwavelink_xwr68xx.aer4f)
target_include_directories(CONTROL_MMWAVELINK INTERFACE ${TI_MMWAVE_PATH}/packages/ti/control/mmwavelink/include
                                                        ${TI_MMWAVE_PATH}/packages/ti/control/mmwavelink)

add_library(CONTROL_MMWAVE STATIC IMPORTED)
SET_TARGET_PROPERTIES(CONTROL_MMWAVE PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/control/mmwave/lib/libmmwave_xwr68xx.aer4f)
target_include_directories(CONTROL_MMWAVE INTERFACE ${TI_MMWAVE_PATH}/packages/ti/control/mmwave/include
                                                    ${TI_MMWAVE_PATH}/packages/ti/control/mmwave)

add_library(MMWAVE_LIB STATIC IMPORTED)
SET_TARGET_PROPERTIES(MMWAVE_LIB PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/alg/mmwavelib/lib/libmmwavealg_xwr68xx.ae674)
target_include_directories(MMWAVE_LIB INTERFACE ${TI_MMWAVE_PATH}/packages/ti/alg/mmwavelib/include
                                                ${TI_MMWAVE_PATH}/packages/ti/alg/mmwavelib)

add_library(MATHUTILS STATIC IMPORTED)
SET_TARGET_PROPERTIES(MATHUTILS PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/utils/mathutils/lib/libmathutils.aer4f)
target_include_directories(MATHUTILS INTERFACE ${TI_MMWAVE_PATH}/packages/ti/utils/mathutils)

add_library(UTILS_CLI STATIC IMPORTED)
SET_TARGET_PROPERTIES(UTILS_CLI PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/utils/cli/lib/libcli_xwr68xx.aer4f)
target_include_directories(UTILS_CLI INTERFACE ${TI_MMWAVE_PATH}/packages/ti/utils/cli/include
                                               ${TI_MMWAVE_PATH}/packages/ti/utils/cli)
