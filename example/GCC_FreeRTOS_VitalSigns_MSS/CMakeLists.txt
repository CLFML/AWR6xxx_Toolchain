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

# Same boot sequence and FreeRTOS-Kernel integration as GCC_FreeRTOS_MSS
# (see that project's CMakeLists.txt for the full explanation of each
# piece below) -- this project reuses the identical toolchain/linker/
# FreeRTOS setup and only adds its own application sources (main.c,
# cli_vitalsigns.c, mbox_task.c) implementing a from-scratch MSS-side
# testbench for a reference vital-signs-tracking demo's MSS<->DSS
# protocol. See src/main.c's header and vitalsigns_mss_testbench.md in
# the AWR6xxx_Toolchain memory notes for what this mimics and why.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/GCC-R4F-toolchain.cmake)

project(gcc_freertos_vitalsigns_mss C ASM)
enable_language(ASM)

# The RadarSS firmware binary (prebuilt/xwr6xxx_radarss_rprc.bin) is vendored
# directly in this repo -- no mmWave SDK install needed for it. It's TI's
# "Radar SS Firmware for IWR68xx" under the "TI Text File" license (see
# mmwave_sdk_[ver]/docs/mmwave_sdk_software_manifest.html), which permits
# unmodified binary redistribution for use with TI Devices provided the
# license text is reproduced alongside it -- see prebuilt/LICENSE-TI-RADARSS.txt.
# prebuilt/testbench_dss.bin is a real compiled DSS build of the same
# reference vital-signs demo this project's MSS side mimics, built from
# that reference's own fw/dss CCS project with VITALSIGNS_TESTBENCH_MODE=1
# (a project-level compiler define added there -- see that project's
# dss_main.c and .cproject) so it skips the mmWaveLink init/sync handshake
# entirely and explicitly acks (MMWDEMO_DSS2MSS_CONFIGDONE) every config
# message this MSS sends -- see mbox_task.c's "DSS: config done" handling.
# Not the same file every other example's prebuilt/placeholder_dss.bin is
# (an unmodified reference DSS build that never acks without a full
# mmWaveLink-driven MSS on the other end -- see mailbox_iwr68xx.c's header
# in Universal_hal for how that one was originally discovered to be real,
# not blank).

# GPIO/pinmux/UART/SOC/Mailbox go through Universal_hal, same as every
# other example. Reused directly from Barebones_MSS's own vendored
# checkout rather than a second clone of the same remote.
set(PLATFORM_DEFINED "SOC_XWR68XX" CACHE STRING "" FORCE)
set(UHAL_DISABLE_ESM_MODULE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../Barebones_MSS/Universal_hal Universal_hal-build)
# AWR6843.h and friends, plus this project's own mmw_messages.h/
# mmw_config.h/mmw_output.h (the MSS<->DSS protocol headers), live in this
# toolchain repo's shared include/ -- see AWR6843.h's header comment and
# mmw_messages.h's header comment for why both kinds of header belong
# there rather than a private per-example copy.
target_include_directories(Universal_hal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)

# ---------------------------------------------------------------------------
# FreeRTOS-Kernel -- identical FetchContent setup to GCC_FreeRTOS_MSS, same
# pinned commit (this project shares that one's hardware-tested tick/IRQ
# glue, ../../src/freertos_iwr68xx.c).
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

# Bisection switch -- see src/main.c's DIAG_MINIMAL_BOOT block. OFF by
# default (zero effect on a normal build); ON strips this project's own
# main() down to something byte-for-byte close to GCC_FreeRTOS_MSS's
# hardware-confirmed boot path (soc_init + UARTA only, no UARTB pinmux/init,
# no mailbox_init, vLedBlinkTask+vUartHelloTask instead of the CLI/mailbox
# tasks) to isolate whether a "flashed but silent, LED off" report is this
# project's own additions or something else.
option(DIAG_MINIMAL_BOOT "Diagnostic: strip main() down to the GCC_FreeRTOS_MSS baseline (no UARTB/mailbox/CLI)" OFF)

# New MSS executable
add_executable(mss_program.elf
    src/main.c
    src/cli_vitalsigns.c
    src/mbox_task.c
    src/startup_awr6843.S
    src/freertos_glue.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/freertos_iwr68xx.c
)
if(DIAG_MINIMAL_BOOT)
    target_compile_definitions(mss_program.elf PRIVATE DIAG_MINIMAL_BOOT=1)
endif()
target_link_libraries(mss_program.elf Universal_hal freertos_kernel)
target_link_options(mss_program.elf PRIVATE
    -T${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.ld
    -Wl,-Map=mss_program.map
)

# ---------------------------------------------------------------------------
# Flashable image generation -- identical pipeline to every other example,
# see Barebones_MSS's CMakeLists.txt for the full explanation.
# ---------------------------------------------------------------------------
set(RADARSS_FW    ${CMAKE_CURRENT_LIST_DIR}/prebuilt/xwr6xxx_radarss_rprc.bin)
set(TESTBENCH_DSS ${CMAKE_CURRENT_LIST_DIR}/prebuilt/testbench_dss.bin)
set(META_IMAGE    gcc_freertos_vitalsigns_mss.bin)

find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

add_custom_command(TARGET mss_program.elf POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable metaimage ${META_IMAGE} (MSS + RadarSS + testbench DSS)"
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../cmake/gen_flash_image.py
            37 0x00000006 ${META_IMAGE}
            0x35510000 $<TARGET_FILE:mss_program.elf>
            0xb5510000 ${RADARSS_FW}
            0xd5510000 ${TESTBENCH_DSS}
)
