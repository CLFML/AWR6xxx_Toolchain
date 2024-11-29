cmake_minimum_required(VERSION 3.14)
set(TI_CGT_ROOT $ENV{HOME}/ti/ti-cgt-arm_16.9.6.LTS)
set(TI_MMWAVE_PATH $ENV{HOME}/ti/mmwave_sdk_03_06_02_00-LTS)
set(TI_LINKER_CMD ${CMAKE_CURRENT_LIST_DIR}/linker_awr6843.cmd)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/R4F-toolchain.cmake)

project(barebones_mss C ASM)

enable_language(ASM)

add_executable(mss_program.xer4f src/main.c)
target_link_libraries(mss_program.xer4f PUBLIC MMWAVE_LIB MMWAVE_DRIVER_SOC MMWAVE_DRIVER_UART MMWAVE_DRIVER_PINMUX MMWAVE_DRIVER_GPIO)

