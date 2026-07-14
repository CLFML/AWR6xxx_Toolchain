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

# Same bare-metal boot as Barebones_MSS, built with arm-none-eabi-gcc instead
# of TI's armcl/armlnk -- a parallel toolchain choice, not a fix for
# anything wrong with the TI build. See src/startup_awr6843.S and
# linker_awr6843.ld for what's genuinely different (assembler/linker-script
# syntax) versus what's identical in substance (the Abort/Undefined-mode
# stack setup, VIM reset, ESM clear that make this boot at all -- see
# Barebones_MSS's git history and the AWR6xxx_Toolchain memory notes).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/GCC-R4F-toolchain.cmake)

project(gcc_mss C ASM)
enable_language(ASM)

# The RadarSS firmware binary (prebuilt/xwr6xxx_radarss_rprc.bin) is vendored
# directly in this repo -- no mmWave SDK install needed for it. It's TI's
# "Radar SS Firmware for IWR68xx" under the "TI Text File" license (see
# mmwave_sdk_[ver]/docs/mmwave_sdk_software_manifest.html), which permits
# unmodified binary redistribution for use with TI Devices provided the
# license text is reproduced alongside it -- see prebuilt/LICENSE-TI-RADARSS.txt.

# GPIO/pinmux/UART/SOC go through Universal_hal, same as Barebones_MSS.
# PLATFORM_DEFINED selects Universal_hal's own CMakeLists.txt branch;
# ESM is disabled for the same reason as Barebones_MSS (esm_init() needs
# SYS/BIOS's Hwi_create(), never used in this bare-metal build -- and
# esm_iwr68xx.c is the one source file on this platform that #includes
# SYS/BIOS's own Hwi.h, which this project has no other reason to need).
# Reused directly from Barebones_MSS's own vendored checkout
# (../Barebones_MSS/Universal_hal) rather than a second clone of the same
# remote -- it's the same HAL, no reason to duplicate a multi-hundred-file
# external repo for a peer example directory. Universal_hal's own C sources
# (gpio_iwr68xx.c etc.) needed no changes for GCC -- plain portable C, no
# TI-compiler-specific constructs; only the hand-written MPU register-access
# assembly (soc_mpu_iwr68xx.asm) needed a GNU-syntax counterpart
# (soc_mpu_iwr68xx_gnu.S), selected automatically by Universal_hal's
# CMakeLists.txt based on CMAKE_C_COMPILER_ID.
set(PLATFORM_DEFINED "SOC_XWR68XX" CACHE STRING "" FORCE)
set(UHAL_DISABLE_ESM_MODULE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../Barebones_MSS/Universal_hal Universal_hal-build)
# AWR6843.h and friends live in this toolchain repo's shared include/
# (CMSIS-style device headers, one copy for every example), not a private
# per-example copy -- see that directory's AWR6843.h header comment.
target_include_directories(Universal_hal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)

# New MSS executable
add_executable(mss_program.elf src/main.c src/startup_awr6843.S)
target_link_libraries(mss_program.elf Universal_hal)
target_link_options(mss_program.elf PRIVATE
    -T${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.ld
    -Wl,-Map=mss_program.map
)

# ---------------------------------------------------------------------------
# Flashable image generation -- identical pipeline to Barebones_MSS, see
# that project's CMakeLists.txt for the full explanation. gen_flash_image.py
# parses standard ELF32 (PT_LOAD segments, SHT_PROGBITS sections) -- nothing
# TI-specific about its input assumptions, so a GCC-produced ELF works the
# same way, confirmed by building this project and diffing section layout.
# ---------------------------------------------------------------------------
set(RADARSS_FW    ${CMAKE_CURRENT_LIST_DIR}/prebuilt/xwr6xxx_radarss_rprc.bin)
set(DSS_PLACEHOLDER ${CMAKE_CURRENT_LIST_DIR}/prebuilt/placeholder_dss.bin)
set(META_IMAGE    gcc_mss.bin)

find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

add_custom_command(TARGET mss_program.elf POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable metaimage ${META_IMAGE} (MSS + RadarSS + DSS placeholder)"
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../cmake/gen_flash_image.py
            37 0x00000006 ${META_IMAGE}
            0x35510000 $<TARGET_FILE:mss_program.elf>
            0xb5510000 ${RADARSS_FW}
            0xd5510000 ${DSS_PLACEHOLDER}
)
