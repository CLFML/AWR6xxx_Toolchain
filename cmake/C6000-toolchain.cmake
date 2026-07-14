# C6000-toolchain.cmake -- cl6x cross-toolchain for the C674x DSP (AWR6843
# DSS), mirroring R4F-toolchain.cmake's role for the MSS/Cortex-R4F side.
# Everything here is ported from that file 1:1 where the underlying issue is
# a CMake/TI-cgtools bug (same shared `Modules/Compiler/TI.cmake` backend
# drives both `armcl` and `cl6x`) rather than something ARM-specific -- see
# each block's comment for which category it falls into. Not yet proven on
# real hardware the way R4F-toolchain.cmake is; this is the DSS toolchain's
# first attempt, expect to iterate.

# CMake re-includes CMAKE_TOOLCHAIN_FILE more than once per configure run
# (once for early system/ABI detection, again once project() actually
# enables the language) -- guard against the add_library()/add_custom_target()
# calls below erroring out as duplicate targets on the second pass.
include_guard(GLOBAL)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR c6000)
set(CMAKE_SYSTEM_VERSION 1)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(SDK_PATH ${TI_CGT_ROOT})
set(SDK_BIN "${SDK_PATH}/bin")

if(NOT CMAKE_FIND_ROOT_PATH)
    set(CMAKE_FIND_ROOT_PATH ${SDK_PATH})
endif()

set(CMAKE_C_COMPILER "${SDK_BIN}/cl6x")
set(CMAKE_CXX_COMPILER "${SDK_BIN}/cl6x")
set(CMAKE_ASM_COMPILER "${SDK_BIN}/cl6x")
set(CMAKE_AR "${SDK_BIN}/ar6x")
set(CMAKE_LINKER "${SDK_BIN}/lnk6x")

# C674x's rts lib, the DSP analog of R4F-toolchain.cmake's
# rtsv7R4_T_le_v3D16_eabi.lib -- ${TI_CGT_ROOT}/lib ships this prebuilt
# already (unlike the R4F one, no setup_runtime_libraries mklib step needed
# here). CONFIRMED via a real link attempt: the "e" suffix in TI's C6000 rts
# lib naming means BIG-endian, not "eabi" as originally guessed here --
# rts6740e_elf.lib failed to link against our little-endian objects with
# "fatal error: object files have incompatible byte orderings". This
# device is little-endian throughout, so the correct lib is the
# NO-suffix one.
set(TI_RTS_LIB "${TI_CGT_ROOT}/lib/rts6740_elf.lib")
set(TI_LIBC_LIB "${TI_CGT_ROOT}/lib/libc.a")

# SOC_XWR68XX/SUBSYS_DSS only -- the truly universal defines, same split as
# R4F-toolchain.cmake (project-specific ones like MMWAVE_L3RAM_SIZE/
# DOWNLOAD_FROM_CCS/DebugP_ASSERT_ENABLED belong in the consuming project's
# own target_compile_definitions(), see example/TI_RTOS_DSS/CMakeLists.txt).
add_compile_definitions(-DSOC_XWR68XX -DSUBSYS_DSS)

# Confirmed against the actual CCS-generated compile command for the
# reference vital-signs demo's dss_main.c (cross-referenced from its own
# Debug/mmwave_src/subdir_rules.mk, not guessed) -- see
# ti_rtos_dss_replica.md in the AWR6xxx_Toolchain memory notes for the
# disassembly-diff confirmation this exact flag set reproduces identical
# code. Note --enum_type=int is deliberately NOT here (unlike R4F's
# equivalent block): the confirmed-correct app compile doesn't use it --
# it's only fed to the SYS/BIOS library build itself via
# dss_rtos.cfg's BIOS.customCCOpts, same split mss_rtos.cfg uses.
add_compile_options(
   --silicon_version=6740
   --abi=eabi
   --opt_for_speed=3
   -g
   --gcc
   --diag_warning=225
   --diag_wrap=off
   --display_error_number
   --gen_func_subsections=on
)

if(TI_DEFER_RTS_LIBS)
   set(_TI_EARLY_RTS_LIBS)
else()
   set(_TI_EARLY_RTS_LIBS ${TI_RTS_LIB} ${TI_LIBC_LIB})
endif()

# --ram_model (not --rom_model) and --stack_size=0x800 are both confirmed
# against the reference vital-signs demo's own DSS link command (its
# Debug/makefile, ground truth same as the compile flags above) -- unlike
# R4F-toolchain.cmake's MSS link, which this block was originally ported
# from 1:1 and got both of these wrong for DSS specifically.
#
# Deliberately NOT including -l${TI_LINKER_CMD} here (unlike
# R4F-toolchain.cmake's equivalent block). TI's linker processes -l<cmdfile>
# positionally, splicing the file's content into the argument stream right
# where it appears; a DSS linker .cmd's MEMORY block commonly evaluates
# --define=d symbols (MMWAVE_L3RAM_NUM_BANK/MMWAVE_SHMEM_BANK_SIZE, sized
# per-project) -- confirmed by a real link failure ("undefined symbol ...
# used in expression") when the .cmd inclusion came from this toolchain-wide
# add_link_options() (always emitted before a target's own
# target_link_options()-added --define=s, since CMake merges directory-scope
# link options before target-scope ones into the same <LINK_FLAGS> slot,
# regardless of CMAKE_C_LINK_EXECUTABLE's own placeholder order). Each DSS
# project must add -l${TI_LINKER_CMD} itself via target_link_options(),
# positioned after its own --define=s.
add_link_options(
   -m "dss_program.xe674.map"
   ${_TI_EARLY_RTS_LIBS}
   --heap_size=0x800
   --stack_size=0x800
   --reread_libs
   --warn_sections
   --ram_model
   --unused_section_elimination=on
)

# Same reason as R4F-toolchain.cmake: TI's cgtools link step doesn't work
# under CMake's own compiler-works check, no easy fix, so disable it.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

include_directories(${TI_CGT_ROOT}/include
   ${TI_CGT_ROOT}/lib/src)

option(USE_TI_RTOS "Use TI RTOS" OFF)

if(USE_TI_RTOS)
    include_directories(${TI_MMWAVE_PATH}/packages
                    ${TI_MMWAVE_PATH}/packages/ti/common)

    # DSS-side driver libraries this toolchain currently links against --
    # grown from a mailbox-only testbench (SOC/OSAL/MAILBOX) to also cover
    # example/TI_RTOS_DSS's dss_main.c dependencies once a real link attempt
    # (see ti_rtos_dss_replica.md) showed it references ADCBuf_*/EDMA_*/
    # MMWave_get*Cfg symbols too (VITALSIGNS_TESTBENCH_MODE compiles out the
    # code that USES them at runtime, but the enclosing non-static functions
    # still have external linkage, so the compiler still emits the calls).
    # Add more MMWAVE_DRIVER_*/MMWAVE_CONTROL_* targets here (.ae674 libs
    # exist in the SDK for most of the same drivers, e.g. libhwa_xwr68xx.ae674,
    # libcbuff_xwr68xx.ae674) only when an actual DSS example needs them.
    add_library(MMWAVE_DRIVER_OSAL STATIC IMPORTED)
    SET_TARGET_PROPERTIES(MMWAVE_DRIVER_OSAL PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/osal/lib/libosal_xwr68xx.ae674)
    target_include_directories(MMWAVE_DRIVER_OSAL INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/osal)

    add_library(MMWAVE_DRIVER_SOC STATIC IMPORTED)
    SET_TARGET_PROPERTIES(MMWAVE_DRIVER_SOC PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/soc/lib/libsoc_xwr68xx.ae674)
    target_include_directories(MMWAVE_DRIVER_SOC INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/soc/include
                                                       ${TI_MMWAVE_PATH}/packages/ti/drivers/soc)

    add_library(MMWAVE_DRIVER_MAILBOX STATIC IMPORTED)
    SET_TARGET_PROPERTIES(MMWAVE_DRIVER_MAILBOX PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/mailbox/lib/libmailbox_xwr68xx.ae674)
    target_include_directories(MMWAVE_DRIVER_MAILBOX INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/mailbox/include
                                                           ${TI_MMWAVE_PATH}/packages/ti/drivers/mailbox)

    add_library(MMWAVE_DRIVER_ADCBUF STATIC IMPORTED)
    SET_TARGET_PROPERTIES(MMWAVE_DRIVER_ADCBUF PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf/lib/libadcbuf_xwr68xx.ae674)
    target_include_directories(MMWAVE_DRIVER_ADCBUF INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf/include
                                                          ${TI_MMWAVE_PATH}/packages/ti/drivers/adcbuf)

    add_library(MMWAVE_DRIVER_EDMA STATIC IMPORTED)
    SET_TARGET_PROPERTIES(MMWAVE_DRIVER_EDMA PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/drivers/edma/lib/libedma_xwr68xx.ae674)
    target_include_directories(MMWAVE_DRIVER_EDMA INTERFACE ${TI_MMWAVE_PATH}/packages/ti/drivers/edma/include
                                                        ${TI_MMWAVE_PATH}/packages/ti/drivers/edma)

    add_library(MMWAVE_CONTROL_MMWAVE STATIC IMPORTED)
    SET_TARGET_PROPERTIES(MMWAVE_CONTROL_MMWAVE PROPERTIES IMPORTED_LOCATION ${TI_MMWAVE_PATH}/packages/ti/control/mmwave/lib/libmmwave_xwr68xx.ae674)
    target_include_directories(MMWAVE_CONTROL_MMWAVE INTERFACE ${TI_MMWAVE_PATH}/packages/ti/control/mmwave/include
                                                           ${TI_MMWAVE_PATH}/packages/ti/control/mmwave)

    set(XDC_OUTPUT_DIR "${CMAKE_BINARY_DIR}/configPkg")

    # ti.targets.elf.C674 / ti.platforms.c6x:IWR68XX:false:600 -- confirmed
    # against the reference vital-signs demo's own DSS CCS project
    # (.cproject's XDC_3.16.tool.TARGET/PLATFORM options), the DSS analog of
    # R4F-toolchain.cmake's ti.targets.arm.elf.R4F / ti.platforms.cortexR
    # (600 = DSP clock MHz, vs. MSS's 200).
    set(TARGET_PLATFORM "\"ti.targets.elf.C674\"")
    set(PLATFORM "ti.platforms.c6x:IWR68XX:false:600")

    set(COMPILE_OPTIONS "\"--enum_type=int\"")

    add_custom_command( OUTPUT "${XDC_OUTPUT_DIR}/package"
                        COMMAND ${TI_XDC_PATH}/xs
                        ARGS --xdcpath="${TI_RTOS_PATH}/packages\;"
                             xdc.tools.configuro
                             -o ${XDC_OUTPUT_DIR}
                             -t ${TARGET_PLATFORM}
                             -p ${PLATFORM}
                             -r release
                             -c ${TI_CGT_ROOT}
                             --compileOptions "${COMPILE_OPTIONS}"
                                              "${TI_RTOS_CONFIG}"
                             WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                             DEPENDS ${TI_RTOS_CONFIG}
                             COMMENT "Running XDCtools configuro"
                      )

    add_custom_target(xdc_gen ALL DEPENDS "${XDC_OUTPUT_DIR}/package")
    add_link_options(-l${XDC_OUTPUT_DIR}/linker.cmd)
endif()
