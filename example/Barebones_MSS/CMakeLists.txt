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
# Set this path to the mmWave SDK (it is already set for default Linux install path)
set(TI_MMWAVE_PATH $ENV{HOME}/ti/mmwave_sdk_03_06_02_00-LTS)

# This path should not be altered, this is the linker file
set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.cmd)

# Load the Toolchain file
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/R4F-toolchain.cmake)

project(barebones_mss C ASM)

# Enable ASM language support, as there is a lot of ASM in the toolchain
enable_language(ASM)

# New MSS executable 
add_executable(mss_program.xer4f src/main.c)
target_link_libraries(mss_program.xer4f PUBLIC MMWAVE_LIB MMWAVE_DRIVER_SOC MMWAVE_DRIVER_UART MMWAVE_DRIVER_PINMUX MMWAVE_DRIVER_GPIO)

