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
# libgpio/libuart/... binaries, and firmware/ImageCreator tooling) that a
# project confirmed booting on this exact board links against/uses. GPIO/
# UART/SOC no longer link against this SDK's precompiled drivers (see
# Universal_hal below), but ESM still does, and the image-gen step still
# uses this SDK's RadarSS firmware/tools. Swapping to another SDK install
# here is an untested variable; match this path exactly unless you've
# verified otherwise.
set(TI_MMWAVE_PATH /mnt/3c8641a4-f089-425f-b228-e740a9f7575e/ti/mmwave_sdk_03_04_00_03)

# This example runs on a minimal SYS/BIOS (TI-RTOS) rather than being truly
# bare-metal. That's deliberate, not an oversight: no hand-rolled bare-metal
# boot has ever been proven to boot on the AWR6843AOP board this was
# developed against -- SYS/BIOS's own startup does essential bring-up that a
# from-scratch reset handler could not practically replicate.
set(USE_TI_RTOS ON)
set(TI_RTOS_PATH $ENV{HOME}/ti/bios_6_73_01_01)
set(TI_XDC_PATH $ENV{HOME}/ti/xdctools_3_50_08_24_core)
set(TI_RTOS_CONFIG ${CMAKE_CURRENT_LIST_DIR}/mss_rtos.cfg)

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

# GPIO/pinmux/UART/SOC now go through Universal_hal (vendored in
# Universal_hal/, gitignored -- see that directory's own git remote) instead
# of the mmWave SDK's precompiled ti/drivers/gpio, ti/drivers/pinmux,
# ti/drivers/uart, ti/drivers/soc.
# PLATFORM_DEFINED selects Universal_hal's own CMakeLists.txt branch that
# builds hal/platform/ti/iwr68xx/{gpio,pinmux,uart,soc}; matched against
# "^SOC_XWR68", not tied to any variable this project already defines
# elsewhere, so set explicitly here rather than reusing SOC_XWR68XX (the
# compile *definition* added by R4F-toolchain.cmake, a different thing).
# TI_RTOS is deliberately NOT defined for this build: Universal_hal's TI
# gpio/pinmux source wraps register access in HwiP_disable()/HwiP_restore()
# under #ifdef TI_RTOS, but doesn't #include the header that declares
# them -- relies on something else in the translation unit doing so first.
# Untested with this SDK version; skip it rather than risk an implicit-
# declaration error under --c99. Losing that interrupt-safety window is
# fine for this single-task blink+hello example.
set(PLATFORM_DEFINED "SOC_XWR68XX" CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Universal_hal Universal_hal-build)
# Universal_hal's own TI source (gpio_iwr68xx.c etc.) includes <AWR6843.h>
# and friends -- those live in this project's include/, not in
# Universal_hal itself, so its own compilation needs this path too, not
# just main.c's.
target_include_directories(Universal_hal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# New MSS executable
add_executable(mss_program.xer4f src/main.c)
add_dependencies(mss_program.xer4f xdc_gen)
target_link_libraries(mss_program.xer4f
                      ${CMAKE_CURRENT_LIST_DIR}/src/sysbios/sysbios.aer4f
                      ${XDC_OUTPUT_DIR}/package/cfg/mss_rtos_per4f.oer4f
                      MMWAVE_DRIVER_ESM
                      MMWAVE_DRIVER_OSAL
                      Universal_hal
                      # Must come LAST -- see TI_DEFER_RTS_LIBS comment above.
                      ${TI_RTS_LIB}
                      ${TI_LIBC_LIB})
include_directories(${XDC_OUTPUT_DIR}
                    ${TI_RTOS_PATH}/packages
                    ${TI_XDC_PATH}/packages) # Include generated headers

# Match the confirmed-working project's exact compile/link flags. Several of
# these (MMWAVE_L3RAM_NUM_BANK, MMWAVE_SHMEM_TCMA/TCMB_NUM_BANK,
# MMWAVE_SHMEM_BANK_SIZE) feed SOC_init()'s own sanity-check asserts against
# the actual hardware memory configuration -- getting them wrong doesn't
# necessarily error at build time, so silently omitting them (as the base
# toolchain file does) is an untested/incorrect configuration, not a merely
# stylistic difference.
target_compile_options(mss_program.xer4f PRIVATE
    -O3
    --c99
    --gen_func_subsections=on
)
target_compile_definitions(mss_program.xer4f PRIVATE
    _LITTLE_ENDIAN
    DOWNLOAD_FROM_CCS
    MMWAVE_L3RAM_SIZE=0xC0000
    DebugP_ASSERT_ENABLED
)
target_link_options(mss_program.xer4f PRIVATE
    --disable_auto_rts
    --define=MMWAVE_L3RAM_NUM_BANK=6
    --define=MMWAVE_SHMEM_TCMA_NUM_BANK=0
    --define=MMWAVE_SHMEM_TCMB_NUM_BANK=0
    --define=MMWAVE_SHMEM_BANK_SIZE=0x20000
    --define=MMWAVE_L3RAM_SIZE=0xC0000
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
