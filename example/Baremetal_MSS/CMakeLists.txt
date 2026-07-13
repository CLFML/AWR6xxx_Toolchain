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

# Toolchain paths
# Make sure you got the ti-cgt-arm-toolchain and mmWave SDK installed!
#
# Set this path to the ti-cgt-arm toolchain (it is already set for default Linux install path)
set(TI_CGT_ROOT $ENV{HOME}/ti/ti-cgt-arm_16.9.6.LTS)
# Set this path to the mmWave SDK. Only used for RadarSS firmware/ImageCreator
# tooling here -- see Barebones_MSS/CMakeLists.txt's longer comment on this;
# nothing in this project links against the mmWave SDK's precompiled drivers.
set(TI_MMWAVE_PATH /mnt/3c8641a4-f089-425f-b228-e740a9f7575e/ti/mmwave_sdk_03_04_00_03)

# This project is genuinely bare-metal -- no SYS/BIOS, no xdc_gen. Unlike
# Barebones_MSS, USE_TI_RTOS is deliberately left OFF/unset (R4F-toolchain.cmake's
# default): no xdc_gen custom target gets created, nothing links sysbios.aer4f
# or configPkg's generated linker.cmd. See src/startup_awr6843.asm's file
# header for why this is expected to work where two earlier bare-metal
# attempts (see Barebones_MSS's git history) didn't.
#
# TI_RTOS_PATH/TI_XDC_PATH are still needed, though: Universal_hal's TI
# platform build unconditionally compiles esm_iwr68xx.c as part of the
# Universal_hal library target (even though this project's main.c never
# calls esm_init() -- Universal_hal doesn't support per-file conditional
# compilation based on what the app actually uses), and that file
# #includes SYS/BIOS's own <ti/sysbios/family/arm/v7r/vim/Hwi.h> +
# <xdc/runtime/Error.h>. Those are just pre-built package headers here,
# not something xdc_gen needs to produce -- see Barebones_MSS/CMakeLists.txt's
# matching comment.
set(TI_RTOS_PATH $ENV{HOME}/ti/bios_6_73_01_01)
set(TI_XDC_PATH $ENV{HOME}/ti/xdctools_3_50_08_24_core)

# This path should not be altered, this is the linker file
set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.cmd)

# This project overrides CMAKE_C_LINK_EXECUTABLE below to use "-z"-style
# linking instead of CMake's default --run_linker mode (which is broken --
# see that override's comment, and Barebones_MSS/CMakeLists.txt's matching
# one for the full story). Under "-z", archive-library link order matters:
# rts.lib/libc.a must come after the objects/libraries that reference their
# symbols. TI_DEFER_RTS_LIBS tells R4F-toolchain.cmake to skip adding them
# early via add_link_options(), so they can be added correctly (last) via
# target_link_libraries() below instead.
set(TI_DEFER_RTS_LIBS ON)

# Load the Toolchain file
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/R4F-toolchain.cmake)

project(baremetal_mss C ASM)

# Enable ASM language support -- src/startup_awr6843.asm is this project's
# vector table + reset trampoline, there is no SYS/BIOS-provided one.
enable_language(ASM)

# Same two CMake-bundled-TI-compiler-support bugs Barebones_MSS/CMakeLists.txt
# works around (see that file's matching comments for the full explanation):
# broken default --run_linker link mode, and a --compile_only/--c_file=
# compile mode that produces objects that link but don't work on real
# hardware.
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> -z -m <TARGET_NAME>.map <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")

# GPIO/pinmux/UART/SOC go through Universal_hal, same as Barebones_MSS --
# see that project's CMakeLists.txt for the full explanation of
# PLATFORM_DEFINED/TI_RTOS. Reused directly from Barebones_MSS's own vendored
# checkout (../Barebones_MSS/Universal_hal) rather than a second clone of the
# same remote -- it's the same HAL, no reason to duplicate a multi-hundred-file
# external repo for a peer example directory.
set(PLATFORM_DEFINED "SOC_XWR68XX" CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../Barebones_MSS/Universal_hal Universal_hal-build)
target_include_directories(Universal_hal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
                                                 ${TI_RTOS_PATH}/packages
                                                 ${TI_XDC_PATH}/packages)

# New MSS executable
add_executable(mss_program.xer4f src/main.c src/startup_awr6843.asm)
target_link_libraries(mss_program.xer4f
                      Universal_hal
                      # Must come LAST -- see TI_DEFER_RTS_LIBS comment above.
                      ${TI_RTS_LIB}
                      ${TI_LIBC_LIB})

target_compile_options(mss_program.xer4f PRIVATE
    -O3
    --c99
    --gen_func_subsections=on
)
target_link_options(mss_program.xer4f PRIVATE
    --disable_auto_rts
    # R4F-toolchain.cmake's --rom_model (added for every project, SYS/BIOS or
    # not) makes the linker require resolving the standard entry symbol
    # _c_int00, regardless of whether anything actually calls it -- and
    # since ${TI_RTS_LIB} is linked (for libc/intrinsics), the linker pulls
    # in that library's boot.obj to satisfy it, which _also_ defines __stack
    # (the standard RTS boot convention name, same one this project's own
    # src/startup_awr6843.asm uses) -> duplicate-symbol link error. Override
    # the entry point to this project's own _resetEntry so _c_int00 is never
    # required and boot.obj never gets pulled in.
    --entry_point=_resetEntry
)

# ---------------------------------------------------------------------------
# Flashable image generation -- identical pipeline to Barebones_MSS, see
# that project's CMakeLists.txt for the full explanation.
# ---------------------------------------------------------------------------
set(RADARSS_FW    ${TI_MMWAVE_PATH}/firmware/radarss/xwr6xxx_radarss_rprc.bin)
set(DSS_PLACEHOLDER ${CMAKE_CURRENT_LIST_DIR}/prebuilt/placeholder_dss.bin)
set(META_IMAGE    baremetal_mss.bin)

find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

add_custom_command(TARGET mss_program.xer4f POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable metaimage ${META_IMAGE} (MSS + RadarSS + DSS placeholder)"
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../cmake/gen_flash_image.py
            37 0x00000006 ${META_IMAGE}
            0x35510000 $<TARGET_FILE:mss_program.xer4f>
            0xb5510000 ${RADARSS_FW}
            0xd5510000 ${DSS_PLACEHOLDER}
)
