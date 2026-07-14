#[[
  Copyright 2024 (C) Jeroen Veen <ducroq> & Victor Hogeweij <Hoog-V>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file is part of the IWR6xxx_Toolchain project

  Author:          Victor Hogeweij <Hoog-V>
]]
cmake_minimum_required(VERSION 3.14)

# DSS/C674x counterpart to TI_RTOS_MSS/CMakeLists.txt. Builds the reference
# vital-signs demo's REAL dss_main.c (copied verbatim into mmwave_src/, see
# ti_rtos_dss_replica.md in the AWR6xxx_Toolchain memory notes), not
# from-scratch source -- the explicit goal of this example is to reproduce
# that project's own CCS build byte-for-byte first, as the validation bar
# before trusting this toolchain for anything new on the DSS side.
# dss_data_path.c and lib/occupancy_detection/lib/common (the real chirp
# data path + OD-zone processing) are deliberately NOT compiled --
# dss_data_path_stub.c supplies empty stand-ins for the handful of externs
# dss_main.c's compiled object still references from them (see that file's
# own comment for the full list and how it was derived).
set(TI_CGT_ROOT $ENV{HOME}/ti/ti-cgt-c6000_8.3.3)
set(TI_MMWAVE_PATH /mnt/3c8641a4-f089-425f-b228-e740a9f7575e/ti/mmwave_sdk_03_04_00_03)

set(USE_TI_RTOS ON)
set(TI_RTOS_PATH $ENV{HOME}/ti/bios_6_73_01_01)
set(TI_XDC_PATH $ENV{HOME}/ti/xdctools_3_50_08_24_core)
set(TI_RTOS_CONFIG ${CMAKE_CURRENT_LIST_DIR}/dss_rtos.cfg)

set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_awr6843_dss.cmd)

# Same reasoning as TI_RTOS_MSS/CMakeLists.txt: "-z"-style linking needs
# rts/libc added LAST via target_link_libraries(), after the objects and
# libraries that reference their symbols.
set(TI_DEFER_RTS_LIBS ON)

# TI's linker processes -l<cmdfile> inclusions positionally, splicing that
# file's content into the argument stream right where it appears -- unlike
# a real C preprocessor, --define=NAME=VALUE only affects .cmd expressions
# parsed AFTER it on the command line. linker_awr6843_dss.cmd's MEMORY
# block evaluates MMWAVE_L3RAM_NUM_BANK/MMWAVE_SHMEM_BANK_SIZE, so this
# project's own --define=s (via target_link_options -> CMake's <LINK_FLAGS>)
# must come BEFORE the toolchain file's -l${TI_LINKER_CMD}/-l<configPkg
# linker.cmd> (via add_link_options -> <CMAKE_C_LINK_FLAGS>) -- confirmed by
# a real link attempt that failed with "undefined symbol ... used in
# expression" when <CMAKE_C_LINK_FLAGS> came first (TI_RTOS_MSS's own
# CMAKE_C_LINK_EXECUTABLE never hit this because linker_awr6843.cmd's MSS
# MEMORY block doesn't reference any --define=d symbols).

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/C6000-toolchain.cmake)

project(ti_rtos_dss C)

# Same CMake/TI-cgtools link-rule bug as TI_RTOS_MSS (shared
# Modules/Compiler/TI.cmake backend between armcl and cl6x) -- CCS's own
# Makefiles link via "cl6x <FLAGS> -z -m <map> ... -o <target> ...", not
# CMake's default --run_linker mode.
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> -z -m <TARGET_NAME>.map <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

# Same CMake/TI-cgtools compile-rule bug as TI_RTOS_MSS: CMake's default
# "--compile_only --c_file=<SOURCE>" combination is dropped in favor of the
# CCS Makefile style (flags first, --output_file=<OBJECT>, source
# positional last) -- this is the exact invocation style validated via the
# disassembly-diff test in ti_rtos_dss_replica.md.
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")

# xwr16xx_cache.c: real (not stubbed) project-local cache-config helper
# functions dss_main.c calls directly -- small, self-contained, no
# datapath/oddemo dependencies, so it's compiled as-is rather than stubbed.
add_executable(dss_program.xe674
    mmwave_src/dss_main.c
    mmwave_src/dss_data_path_stub.c
    mmwave_src/lib/dss/cache/xwr16xx_cache.c
)
add_dependencies(dss_program.xe674 xdc_gen)
target_link_libraries(dss_program.xe674
                      ${XDC_OUTPUT_DIR}/package/cfg/dss_rtos_pe674.oe674
                      MMWAVE_DRIVER_OSAL
                      MMWAVE_DRIVER_SOC
                      MMWAVE_DRIVER_MAILBOX
                      MMWAVE_DRIVER_ADCBUF
                      MMWAVE_DRIVER_EDMA
                      MMWAVE_CONTROL_MMWAVE
                      # Must come LAST -- see TI_DEFER_RTS_LIBS comment above.
                      ${TI_RTS_LIB}
                      ${TI_LIBC_LIB})
include_directories(${XDC_OUTPUT_DIR}
                    ${TI_RTOS_PATH}/packages
                    ${TI_XDC_PATH}/packages
                    # Project-local include paths, taken verbatim from the
                    # reference project's own CCS-generated
                    # Debug/mmwave_src/subdir_rules.mk (ground truth, see
                    # ti_rtos_dss_replica.md).
                    ${CMAKE_CURRENT_LIST_DIR}
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/dss
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/common
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/dss/data_path
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/dss/cache
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/dss/edma
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/dss/types
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/dss/util
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/occupancy_detection
                    ${CMAKE_CURRENT_LIST_DIR}/mmwave_src/lib/utils
                    ${TI_MMWAVE_PATH}/packages/ti/alg/mmwavelib/include
                    /home/victor/ti/mathlib_c674x_3_1_2_1/packages
                    /home/victor/ti/dsplib_c64Px_3_4_0_0/packages)

# Confirmed-correct defines for this exact file, taken verbatim from the
# reference project's own CCS-generated subdir_rules.mk (see
# ti_rtos_dss_replica.md for how these were extracted/validated).
target_compile_options(dss_program.xe674 PRIVATE
    -O3
)
target_compile_definitions(dss_program.xe674 PRIVATE
    MMWAVE_L3RAM_SIZE=0xC0000
    MMWAVE_L3RAM_NUM_BANK=6
    MMWAVE_SHMEM_BANK_SIZE=0x20000
    DOWNLOAD_FROM_CCS
    DebugP_ASSERT_ENABLED
    VITALSIGNS_TESTBENCH_MODE=1
)
# -l${TI_LINKER_CMD} must come AFTER the --define=s above -- see the "not
# including -l${TI_LINKER_CMD}" comment in C6000-toolchain.cmake for why.
target_link_options(dss_program.xe674 PRIVATE
    --disable_auto_rts
    --define=MMWAVE_L3RAM_NUM_BANK=6
    --define=MMWAVE_SHMEM_TCMA_NUM_BANK=0
    --define=MMWAVE_SHMEM_TCMB_NUM_BANK=0
    --define=MMWAVE_SHMEM_BANK_SIZE=0x20000
    --define=MMWAVE_L3RAM_SIZE=0xC0000
    -l${TI_LINKER_CMD}
)
