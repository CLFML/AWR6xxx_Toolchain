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

# Same boot sequence as GCC_FreeRTOS_MSS (see that project's CMakeLists.txt
# and src/startup_awr6843.S) -- this project's own contribution is
# cli_dss_probe.c/dss_mem.c (MSS-as-debug-probe-for-DSS: peek/poke/load
# DSS's own L2/L3 memory directly, then release its power-on halt), not a
# different boot path.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/GCC-R4F-toolchain.cmake)

project(gcc_freertos_dss_probe_mss C ASM)
enable_language(ASM)

# The RadarSS firmware binary (prebuilt/xwr6xxx_radarss_rprc.bin) is vendored
# directly in this repo -- no mmWave SDK install needed for it. It's TI's
# "Radar SS Firmware for IWR68xx" under the "TI Text File" license (see
# mmwave_sdk_[ver]/docs/mmwave_sdk_software_manifest.html), which permits
# unmodified binary redistribution for use with TI Devices provided the
# license text is reproduced alongside it -- see prebuilt/LICENSE-TI-RADARSS.txt.
# prebuilt/placeholder_dss.bin is the same generic placeholder every other
# example here flashes -- it doesn't matter what's in it for this project,
# since DSS stays halted (and its memory is expected to be overwritten via
# `dssLoad`) until the CLI's `dssStart` command runs.

# GPIO/pinmux/UART/SOC go through Universal_hal, same as every other
# example -- see GCC_FreeRTOS_MSS/CMakeLists.txt's identical block for the
# full explanation of PLATFORM_DEFINED/UHAL_DISABLE_ESM_MODULE and why this
# reuses Barebones_MSS's checkout rather than a private clone.
set(PLATFORM_DEFINED "SOC_XWR68XX" CACHE STRING "" FORCE)
set(UHAL_DISABLE_ESM_MODULE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../Barebones_MSS/Universal_hal Universal_hal-build)
target_include_directories(Universal_hal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)

# ---------------------------------------------------------------------------
# FreeRTOS-Kernel -- identical setup to GCC_FreeRTOS_MSS/CMakeLists.txt, see
# that file for the full explanation of each option.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        9db704cd3bbe4963f9bd3de1f1161ea2cf90768b
)

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)

set(FREERTOS_PORT GCC_ARM_CRX_NOGIC CACHE STRING "" FORCE)
set(FREERTOS_HEAP "4" CACHE STRING "" FORCE)

FetchContent_MakeAvailable(freertos_kernel)

# New MSS executable
add_executable(mss_program.elf
    src/main.c
    src/startup_awr6843.S
    src/freertos_glue.c
    src/cli_dss_probe.c
    src/dss_mem.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/freertos_iwr68xx.c
)
target_link_libraries(mss_program.elf Universal_hal freertos_kernel)
target_link_options(mss_program.elf PRIVATE
    -T${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.ld
    -Wl,-Map=mss_program.map
)

# ---------------------------------------------------------------------------
# Flashable image generation -- identical pipeline to every other GCC-based
# example, see GCC_FreeRTOS_MSS/CMakeLists.txt for the full explanation.
# ---------------------------------------------------------------------------
set(RADARSS_FW    ${CMAKE_CURRENT_LIST_DIR}/prebuilt/xwr6xxx_radarss_rprc.bin)
set(DSS_PLACEHOLDER ${CMAKE_CURRENT_LIST_DIR}/prebuilt/placeholder_dss.bin)
set(META_IMAGE    gcc_freertos_dss_probe_mss.bin)

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
