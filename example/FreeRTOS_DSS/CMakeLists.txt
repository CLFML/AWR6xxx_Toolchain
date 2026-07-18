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

# FreeRTOS on the DSS (C674x/C64x+), no SYS/BIOS/XDCtools at all -- see
# dss_freertos_port.md in the AWR6xxx_Toolchain memory notes for the full
# design story. Phase 1 only: prove the port layer itself (src/port/,
# scheduler + tick + task switch) with a minimal two-task bring-up
# (src/main.c) -- NOT yet example/TI_RTOS_DSS's dss_main.c application,
# that's a follow-up once this is hardware-confirmed.
#
# Still built with cl6x (USE_TI_RTOS is deliberately left OFF -- no
# xdc_gen, no configPkg, nothing XDCtools-related): there is no C6000 GCC
# backend, so cl6x remains the only compiler option regardless of RTOS
# choice.
set(TI_CGT_ROOT $ENV{HOME}/ti/ti-cgt-c6000_8.3.3)

set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_freertos_dss.cmd)
set(TI_DEFER_RTS_LIBS ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/C6000-toolchain.cmake)

project(freertos_dss C ASM)
enable_language(ASM)

set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> -z -m <TARGET_NAME>.map <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")
# No CMAKE_ASM_COMPILE_OBJECT override -- unlike the C compile rule above,
# CMake's default TI-compiler ASM rule has never needed one anywhere else
# in this toolchain (R4F-toolchain.cmake's own consumers assemble .asm/.S
# files with no override either), so this follows that same precedent
# rather than introducing an untested one.

# ---------------------------------------------------------------------------
# FreeRTOS-Kernel source -- fetched the same way every GCC_FreeRTOS_*
# example on the MSS side does (same pinned commit, for consistency), but
# NOT built via its own top-level CMakeLists.txt: that file's portable-
# library selection only knows about upstream-listed ports (ARM/RISC-V/
# MSP430/x86/...), nothing for C6000. FetchContent_Populate() (rather than
# FetchContent_MakeAvailable()) fetches the source tree without invoking
# its build system at all, so this project pulls in exactly the kernel
# source files it needs directly, with portmacro.h supplied from this
# project's own src/port/ instead.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        9db704cd3bbe4963f9bd3de1f1161ea2cf90768b
)
FetchContent_Populate(freertos_kernel)

add_executable(dss_program.xe674
    src/main.c
    src/startup_dss.asm
    src/port/port_c674.c
    src/port/dss_tick_timer.c
    src/port/hwi_dispatch.c
    src/port/Hwi_disp_always.asm
    src/port/TaskSupport_asm.asm
    ${freertos_kernel_SOURCE_DIR}/tasks.c
    ${freertos_kernel_SOURCE_DIR}/queue.c
    ${freertos_kernel_SOURCE_DIR}/list.c
    ${freertos_kernel_SOURCE_DIR}/timers.c
    ${freertos_kernel_SOURCE_DIR}/portable/MemMang/heap_4.c
)
target_include_directories(dss_program.xe674 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src/port
    ${freertos_kernel_SOURCE_DIR}/include
)
target_link_libraries(dss_program.xe674
    # Must come LAST -- see TI_DEFER_RTS_LIBS comment in C6000-toolchain.cmake.
    ${TI_RTS_LIB}
    ${TI_LIBC_LIB}
)
# cl6x defaults scalar globals to --mem_model:data=near -- STATIC_BASE
# (B14/DP)-relative addressing with a 15-bit UNSIGNED offset field, so
# every such symbol must sit within 32KB of wherever the linker anchors
# STATIC_BASE (this project's .text, at 0x7E0000). This project's own
# .rodata/.bss/.neardata (linker_freertos_dss.cmd's GROUP) land ~145KB
# away -- a real, fatal link warning ("output file cannot be loaded and
# run on a target system"), not cosmetic; TI_RTOS_DSS never hits this
# since XDCtools' own SYS/BIOS build flags already default appropriately
# for that project. --mem_model:data=far makes ALL data absolutely
# addressed instead, removing the proximity constraint entirely -- the
# robust fix, since this program will only grow once dss_main.c's
# application logic is integrated (a temporary heap-size squeeze alone
# barely moved the overflow -- .text/.data separation dominates it, not
# any one object's size).
target_compile_options(dss_program.xe674 PRIVATE
    --mem_model:data=far
)
target_link_options(dss_program.xe674 PRIVATE
    -l${TI_LINKER_CMD}
)

# ---------------------------------------------------------------------------
# Flashable DSS-slot component generation -- same --to-rprc mode
# example/TI_RTOS_DSS's own CMakeLists.txt uses (see that file's own
# comment for the full explanation): no MSS component here to fuse into a
# full metaimage, so this produces a standalone RPRC-encoded dss_program.bin
# any MSS example's own DSS_PLACEHOLDER can point at instead.
# ---------------------------------------------------------------------------
set(DSS_RPRC_OUTPUT dss_program.bin)

find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

add_custom_command(TARGET dss_program.xe674 POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable DSS-slot component ${DSS_RPRC_OUTPUT}"
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../cmake/gen_flash_image.py
            --to-rprc $<TARGET_FILE:dss_program.xe674> ${DSS_RPRC_OUTPUT}
)
