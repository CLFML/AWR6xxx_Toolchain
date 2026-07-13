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
# Set this path to the mmWave SDK.
# Deliberately mmwave_sdk_03_04_00_03, NOT the 03_06_02_00-LTS install under
# $HOME/ti -- this is the exact SDK version (different compiled
# libgpio/libuart/libsoc/... binaries) that a project confirmed booting on
# this exact board links against. Swapping to another SDK install here is an
# untested variable; match this path exactly unless you've verified otherwise.
set(TI_MMWAVE_PATH /mnt/3c8641a4-f089-425f-b228-e740a9f7575e/ti/mmwave_sdk_03_04_00_03)

# Bare-metal: no SYS/BIOS, no XDC/configuro (USE_TI_RTOS stays OFF, the
# toolchain file's default). src/startup_awr6843.asm supplies the vector
# table and reset trampoline instead of relying on sysbios.aer4f's linked-in
# startup -- see that file and src/main.c's header comments (piece 4/N) for
# why this is now expected to work, unlike the very first bare-metal attempt.

# This path should not be altered, this is the linker file
set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.cmd)

# This project overrides CMAKE_C_LINK_EXECUTABLE below to use "-z"-style
# linking instead of CMake's default --run_linker mode (which is broken --
# see that override's comment). Under "-z", archive-library link order
# matters: rts.lib/libc.a must come after the objects/libraries that
# reference their symbols. TI_DEFER_RTS_LIBS tells R4F-toolchain.cmake to
# skip adding them early via add_link_options(), so they can be added
# correctly (last) via target_link_libraries() below instead.
set(TI_DEFER_RTS_LIBS ON)

# Load the Toolchain file
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/R4F-toolchain.cmake)

project(barebones_mss C ASM)

# Enable ASM language support, as there is a lot of ASM in the toolchain
enable_language(ASM)

# CMake's own bundled TI compiler support (Modules/Compiler/TI.cmake) links
# executables via "armcl <FLAGS> --run_linker --output_file=<TARGET> ...".
# That mode silently rejects several linker options as invalid (e.g.
# --code_state=16/--float_support=VFPv3D16, ignored with a warning) and, in
# this project, produced a linked .xer4f malformed enough that out2rprc.exe
# crashed with an unhandled .NET exception parsing it -- a corrupt image was
# still emitted, no build error. TI's classic implicit-link invocation
# ("armcl ... -z -m <map> ... -o <target> ...", the style CCS-exported
# Makefiles use) does not have this problem: isolating this exact difference
# reproduced the confirmed-working image byte-for-byte. Override CMake's link
# rule to use that style for this project.
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> -z -m <TARGET_NAME>.map <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

# Same class of bug as the link rule above: CMake's bundled TI compiler
# support compiles via "armcl --compile_only --c_file=<SOURCE> ...
# --output_file=<OBJECT>". That combination produces an object file that
# links and runs, but is subtly non-functional on real hardware (confirmed by
# isolating this exact difference: relinking a proven-working object through
# this project's own verified-correct link/configPkg/sysbios environment
# worked, while an object compiled via CMake's default rule from identical
# source did not). Dropping --compile_only/--c_file= in favor of the CCS
# Makefile style (flags first, --output_file=<OBJECT>, then the source
# positionally) produces byte-identical code (confirmed via disassembly diff
# -- only an embedded filename and a random compiler-internal build tag
# differ).
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")

# Same rule, applied to the assembly source (src/startup_awr6843.asm) for
# consistency -- armcl compiles/assembles through the same front end either
# way.
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")

# New MSS executable
add_executable(mss_program.xer4f src/main.c src/startup_awr6843.asm)
target_include_directories(mss_program.xer4f PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(mss_program.xer4f
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
)

# ---------------------------------------------------------------------------
# Flashable image generation
#
# The radar ROM/SBL cannot boot a raw .xer4f ELF. It boots a multicore
# "metaimage": the MSS application (converted to RPRC) fused with the RadarSS
# (BSS) firmware and a DSS component, with CRCs inserted. The result
# (barebones_mss.bin) is what you flash over UART with UniFlash -- see
# FLASHING.md.
#
# TI ships these tools as Windows binaries: out2rprc is a .NET assembly (run
# with mono); the other three are native Win32 (run with wine).
#
# The DSS component (prebuilt/placeholder_dss.bin) is a placeholder reused
# from a project confirmed booting on this exact board -- this MSS-only
# example has no DSS application of its own, but DSS-component necessity for
# this SBL was never conclusively ruled out during development, so it's kept
# for safety. A future improvement would be to build a trivial do-nothing DSS
# stub instead of shipping someone else's binary.
# ---------------------------------------------------------------------------
set(IMAGE_CREATOR ${TI_MMWAVE_PATH}/packages/scripts/ImageCreator)
set(RADARSS_FW    ${TI_MMWAVE_PATH}/firmware/radarss/xwr6xxx_radarss_rprc.bin)
set(DSS_PLACEHOLDER ${CMAKE_CURRENT_LIST_DIR}/prebuilt/placeholder_dss.bin)
set(META_IMAGE    barebones_mss.bin)

find_program(MONO_EXECUTABLE mono REQUIRED)
find_program(WINE_EXECUTABLE wine REQUIRED)

add_custom_command(TARGET mss_program.xer4f POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable metaimage ${META_IMAGE} (MSS + RadarSS + DSS placeholder)"
    # 1. ELF -> RPRC
    COMMAND ${MONO_EXECUTABLE} ${IMAGE_CREATOR}/out2rprc/out2rprc.exe
            $<TARGET_FILE:mss_program.xer4f> mss_program.rprc
    # 2. Fuse MSS RPRC (0x35510000) + RadarSS firmware (0xb5510000) + DSS (0xd5510000)
    COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${WINE_EXECUTABLE}
            ${IMAGE_CREATOR}/multicore_image_generator/MulticoreImageGen.exe
            LE 37 0x00000006 ${META_IMAGE}
            0x35510000 mss_program.rprc
            0xb5510000 ${RADARSS_FW}
            0xd5510000 ${DSS_PLACEHOLDER}
    # 3. Insert per-section CRCs, then 4. append the trailing image CRC32
    COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${WINE_EXECUTABLE}
            ${IMAGE_CREATOR}/crc_multicore_image/crc_multicore_image.exe
            ${META_IMAGE} ${META_IMAGE}.tmp
    COMMAND ${CMAKE_COMMAND} -E env WINEDEBUG=-all ${WINE_EXECUTABLE}
            ${IMAGE_CREATOR}/append_bin_crc/gen_bincrc32.exe ${META_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E echo "Flashable image ready: ${CMAKE_BINARY_DIR}/${META_IMAGE}"
)
