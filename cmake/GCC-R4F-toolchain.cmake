# GCC-R4F-toolchain.cmake -- arm-none-eabi-gcc cross-toolchain for the
# Cortex-R4F (AWR6843 MSS), mirroring R4F-toolchain.cmake's role for TI's
# armcl but for GCC. A parallel toolchain choice, not a replacement -- both
# files coexist; a project includes whichever one it wants to build with.
#
# No CMAKE_C_LINK_EXECUTABLE/CMAKE_C_COMPILE_OBJECT overrides here: those
# existed in R4F-toolchain.cmake to work around two real bugs in CMake's
# bundled TI-compiler support (Modules/Compiler/TI.cmake). CMake's GCC
# cross-compilation support is a much more standard, well-trodden path and
# doesn't have that class of problem.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSTEM_VERSION 1)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(GCC_R4F_C_COMPILER arm-none-eabi-gcc REQUIRED)
find_program(GCC_R4F_AR arm-none-eabi-ar REQUIRED)
find_program(GCC_R4F_OBJCOPY arm-none-eabi-objcopy REQUIRED)

set(CMAKE_C_COMPILER ${GCC_R4F_C_COMPILER})
set(CMAKE_ASM_COMPILER ${GCC_R4F_C_COMPILER})
set(CMAKE_AR ${GCC_R4F_AR})

# Linking a bare-metal test executable during CMake's compiler-ABI-detection
# step would fail without a linker script/entry point -- same chicken-and-
# egg problem R4F-toolchain.cmake solves by disabling the check outright.
# CXX is included too because Universal_hal's own project(Universal_hal)
# call (no explicit LANGUAGES list) implicitly enables it, even though
# nothing in this build actually compiles any C++.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# -mcpu=cortex-r4f -mfpu=vfpv3-d16 -mfloat-abi=hard: matches TI's
# -mv7R4/--float_support=VFPv3D16 for the same silicon (Cortex-R4F, VFPv3-D16
# FPU). -mthumb: matches TI's --code_state=16 (Thumb-2 code generation);
# src/startup_awr6843.S is explicitly .arm internally regardless of this
# default (required for the vector table / exception-mode setup), and GNU
# ld's automatic ARM/Thumb interworking veneers handle the `bl main` call
# from that ARM-mode file into Thumb-mode C code transparently -- the same
# ARM-calls-Thumb-via-BL pattern TI's own boot.asm/_c_int00 already proves
# works on this exact target.
set(GCC_R4F_CPU_FLAGS -mcpu=cortex-r4f -mfpu=vfpv3-d16 -mfloat-abi=hard -mthumb)

add_compile_options(${GCC_R4F_CPU_FLAGS} -Wall -ffreestanding -fno-unwind-tables)
add_compile_definitions(SOC_XWR68XX SUBSYS_MSS)

# -nostartfiles: this project provides its own reset entry point
# (_resetEntry, see startup_awr6843.S) instead of newlib's crt0/_start,
# which would assume a different boot sequence. Deliberately NOT
# -nostdlib/-nodefaultlibs: libgcc (integer divide helpers -- Cortex-R4F has
# no hardware SDIV/UDIV) and a minimal libc (memcpy/memset, which compiler-
# generated code can emit for struct assignment/array init even though this
# project never calls them directly) both stay linked.
# --specs=nosys.specs: provides stub implementations of the syscalls
# (_sbrk/_write/_read/_exit/...) newlib's libc references internally, in
# case anything pulls them in transitively -- standard for bare-metal
# newlib targets with no OS underneath.
add_link_options(${GCC_R4F_CPU_FLAGS} -nostartfiles --specs=nosys.specs -Wl,--gc-sections)
