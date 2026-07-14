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
# The RadarSS firmware binary (prebuilt/xwr6xxx_radarss_rprc.bin) is vendored
# directly in this repo -- no mmWave SDK install needed for it. It's TI's
# "Radar SS Firmware for IWR68xx" under the "TI Text File" license (see
# mmwave_sdk_[ver]/docs/mmwave_sdk_software_manifest.html), which permits
# unmodified binary redistribution for use with TI Devices provided the
# license text is reproduced alongside it -- see prebuilt/LICENSE-TI-RADARSS.txt.

# This project is genuinely bare-metal -- no SYS/BIOS, no xdc_gen, and (as of
# UHAL_DISABLE_ESM_MODULE below) no SYS/BIOS/XDCtools install needed at all.
# USE_TI_RTOS is deliberately left OFF/unset (R4F-toolchain.cmake's default):
# no xdc_gen custom target gets created, nothing links sysbios.aer4f or a
# generated configPkg linker.cmd. See src/startup_awr6843.asm's file header
# for why this is expected to work where two earlier bare-metal attempts
# (see this project's own git history) didn't -- confirmed booting on real
# hardware.
#
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

# Enable ASM language support -- src/startup_awr6843.asm is this project's
# vector table + reset trampoline, there is no SYS/BIOS-provided one.
enable_language(ASM)

# CMake's own bundled TI compiler support (Modules/Compiler/TI.cmake) links
# executables via "armcl <FLAGS> --run_linker --output_file=<TARGET> ...".
# That mode silently rejects several linker options as invalid (e.g.
# --code_state=16/--float_support=VFPv3D16, ignored with a warning) and, in
# this project, produced a linked .xer4f malformed enough that the image-gen
# tooling choked on it -- while CMake still reports a "successful" build.
# TI's classic implicit-link invocation ("armcl ... -z -m <map> ... -o
# <target> ...", the style CCS-exported Makefiles use) does not have this
# problem. Override CMake's link rule to use that style for this project.
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> -z -m <TARGET_NAME>.map <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

# Same class of bug as the link rule above: CMake's bundled TI compiler
# support compiles via "armcl --compile_only --c_file=<SOURCE> ...
# --output_file=<OBJECT>". That combination produces an object file that
# links and runs, but is subtly non-functional on real hardware (confirmed by
# isolating this exact difference). Dropping --compile_only/--c_file= in
# favor of the CCS Makefile style (flags first, --output_file=<OBJECT>, then
# the source positionally) produces byte-identical code.
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")

# GPIO/pinmux/UART/SOC all go through Universal_hal (vendored in
# Universal_hal/, gitignored -- see that directory's own git remote) instead
# of the mmWave SDK's precompiled ti/drivers/*.
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
#
# ESM is disabled outright: esm_init() is never called from this bare-metal
# main() (it needs SYS/BIOS's Hwi_create() to install the FIQ/IRQ handlers,
# see src/main.c), and esm_iwr68xx.c is the one Universal_hal source file
# for this platform that #includes SYS/BIOS's own Hwi.h -- without this,
# Universal_hal's build would require a SYS/BIOS/XDCtools install just to
# satisfy an #include this project never exercises.
set(PLATFORM_DEFINED "SOC_XWR68XX" CACHE STRING "" FORCE)
set(UHAL_DISABLE_ESM_MODULE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Universal_hal Universal_hal-build)
# Universal_hal's own TI source (gpio_iwr68xx.c etc.) includes <AWR6843.h>
# and friends -- those live in this toolchain repo's shared include/ (CMSIS-
# style device headers, one copy for every example -- see that directory's
# AWR6843.h header comment for why this isn't inside Universal_hal itself),
# not a private per-example copy, so Universal_hal's own compilation needs
# this path too, not just main.c's.
target_include_directories(Universal_hal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)

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
    # R4F-toolchain.cmake's --rom_model (added unconditionally) makes the
    # linker require resolving the standard entry symbol _c_int00,
    # regardless of whether anything actually calls it -- and since
    # ${TI_RTS_LIB} is linked (for libc/intrinsics), the linker pulls in
    # that library's boot.obj to satisfy it, which _also_ defines __stack
    # (the standard RTS boot convention name, same one this project's own
    # src/startup_awr6843.asm uses) -> duplicate-symbol link error. Override
    # the entry point to this project's own _resetEntry so _c_int00 is never
    # required and boot.obj never gets pulled in.
    --entry_point=_resetEntry
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
# TI ships the tools for this as four separate Windows binaries (one .NET,
# three native PE) run via mono/wine. All four are replaced here by
# cmake/gen_flash_image.py, a single pure-Python script covering the whole
# ELF->RPRC->meta-image->CRC64->CRC32 pipeline in memory (no mono, no wine,
# no intermediate files). No TI format spec exists for any of these
# stages; each was reverse-engineered separately -- see gen_flash_image.py's
# module docstring for exactly what was verified and how.
#
# The DSS component (prebuilt/placeholder_dss.bin) is a placeholder reused
# from a project confirmed booting on this exact board -- this MSS-only
# example has no DSS application of its own, but DSS-component necessity for
# this SBL was never conclusively ruled out during development, so it's kept
# for safety. A future improvement would be to build a trivial do-nothing DSS
# stub instead of shipping someone else's binary.
# ---------------------------------------------------------------------------
set(RADARSS_FW    ${CMAKE_CURRENT_LIST_DIR}/prebuilt/xwr6xxx_radarss_rprc.bin)
set(DSS_PLACEHOLDER ${CMAKE_CURRENT_LIST_DIR}/prebuilt/placeholder_dss.bin)
set(META_IMAGE    barebones_mss.bin)

find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

add_custom_command(TARGET mss_program.xer4f POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable metaimage ${META_IMAGE} (MSS + RadarSS + DSS placeholder)"
    # Fuse MSS (0x35510000, ELF auto-converted to RPRC) + RadarSS firmware
    # (0xb5510000) + DSS (0xd5510000) into one flashable image.
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../cmake/gen_flash_image.py
            37 0x00000006 ${META_IMAGE}
            0x35510000 $<TARGET_FILE:mss_program.xer4f>
            0xb5510000 ${RADARSS_FW}
            0xd5510000 ${DSS_PLACEHOLDER}
)
