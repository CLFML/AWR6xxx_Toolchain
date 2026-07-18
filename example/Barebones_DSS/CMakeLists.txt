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

# Minimal, scheduler-free tick-interrupt diagnostic for the AWR6843 DSS
# (C674x/C64x+) -- no FreeRTOS, no SYS/BIOS/XDCtools. See
# dss_freertos_port.md in the AWR6xxx_Toolchain memory notes for why
# this exists: isolating whether the nondeterministic INT14/INT15
# dispatch hazard found in example/FreeRTOS_DSS is specific to that
# project's task-switch/critical-section machinery, or fundamental.
set(TI_CGT_ROOT $ENV{HOME}/ti/ti-cgt-c6000_8.3.3)

set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_barebones_dss.cmd)
set(TI_DEFER_RTS_LIBS ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/C6000-toolchain.cmake)

project(barebones_dss C ASM)
enable_language(ASM)

set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> -z -m <TARGET_NAME>.map <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT> <SOURCE>")

add_executable(dss_program.xe674
    src/main.c
    src/startup_dss.asm
    src/dss_tick_timer.c
)
target_link_libraries(dss_program.xe674
    # Must come LAST -- see TI_DEFER_RTS_LIBS comment in C6000-toolchain.cmake.
    ${TI_RTS_LIB}
    ${TI_LIBC_LIB}
)
# Same rationale as example/FreeRTOS_DSS's own CMakeLists.txt -- see that
# file's comment for the full explanation of why --mem_model:data=far is
# required (STATIC_BASE-relative near-data addressing's 15-bit offset
# field can't reach .rodata/.bss/.neardata from this linker script's
# .text anchor).
target_compile_options(dss_program.xe674 PRIVATE
    --mem_model:data=far
)
target_link_options(dss_program.xe674 PRIVATE
    -l${TI_LINKER_CMD}
)

# ---------------------------------------------------------------------------
# Flashable DSS-slot component generation -- same --to-rprc mode every
# other standalone-DSS example in this toolchain uses.
# ---------------------------------------------------------------------------
set(DSS_RPRC_OUTPUT dss_program.bin)

find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

add_custom_command(TARGET dss_program.xe674 POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating flashable DSS-slot component ${DSS_RPRC_OUTPUT}"
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../cmake/gen_flash_image.py
            --to-rprc $<TARGET_FILE:dss_program.xe674> ${DSS_RPRC_OUTPUT}
)
