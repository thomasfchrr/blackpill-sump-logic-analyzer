# Toolchain file: LLVM Clang + LLD for Cortex-M4F, newlib-nano and libgcc taken from the
# Arm GNU toolchain shipped with STM32CubeIDE (no GCC code is compiled, only its C library).
#
# Optional overrides (cache variables or environment):
#   CLANG_DIR       directory holding clang and ld.lld
#   ARM_GNU_DIR     directory holding arm-none-eabi-gcc (used only to locate libc/libgcc, objcopy, size)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT CLANG_DIR AND DEFINED ENV{CLANG_DIR})
  set(CLANG_DIR "$ENV{CLANG_DIR}")
endif()
if(NOT ARM_GNU_DIR AND DEFINED ENV{ARM_GNU_DIR})
  set(ARM_GNU_DIR "$ENV{ARM_GNU_DIR}")
endif()

find_program(CLANG_EXE clang HINTS "${CLANG_DIR}" REQUIRED)
find_program(LLD_EXE ld.lld HINTS "${CLANG_DIR}" REQUIRED)

file(GLOB _cube_gcc_bins
  "C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin")
find_program(ARM_GCC arm-none-eabi-gcc HINTS "${ARM_GNU_DIR}" ${_cube_gcc_bins} REQUIRED)

# binutils (objcopy/size) come from the same Arm GNU bundle; the compiler stays Clang.
cmake_path(GET ARM_GCC PARENT_PATH _gnu_bin)
find_program(OBJCOPY_EXE arm-none-eabi-objcopy HINTS "${_gnu_bin}" REQUIRED)
find_program(SIZE_EXE arm-none-eabi-size HINTS "${_gnu_bin}" REQUIRED)

set(_cpu -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
foreach(_q libgcc.a libc_nano.a libm.a)
  execute_process(COMMAND "${ARM_GCC}" ${_cpu} -print-file-name=${_q}
                  OUTPUT_VARIABLE _p OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REPLACE "." "_" _v "${_q}")
  cmake_path(NORMAL_PATH _p)
  set(ARM_${_v} "${_p}" CACHE INTERNAL "")
endforeach()
cmake_path(GET ARM_libc_nano_a PARENT_PATH _libdir)
# <gnu>/arm-none-eabi/lib/thumb/v7e-m+fp/hard -> <gnu>/arm-none-eabi/include
cmake_path(ABSOLUTE_PATH _libdir BASE_DIRECTORY / NORMALIZE OUTPUT_VARIABLE _libdir)
set(ARM_NEWLIB_INCLUDE "${_libdir}/../../../../include" CACHE INTERNAL "")
cmake_path(NORMAL_PATH ARM_NEWLIB_INCLUDE)

set(CMAKE_C_COMPILER   "${CLANG_EXE}")
set(CMAKE_ASM_COMPILER "${CLANG_EXE}")
set(CMAKE_C_COMPILER_TARGET   thumbv7em-none-eabihf)
set(CMAKE_ASM_COMPILER_TARGET thumbv7em-none-eabihf)

set(_flags "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -ffunction-sections -fdata-sections -fno-common -ffreestanding -isystem \"${ARM_NEWLIB_INCLUDE}\"")
set(CMAKE_C_FLAGS_INIT   "${_flags}")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Wno-unused-command-line-argument")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
