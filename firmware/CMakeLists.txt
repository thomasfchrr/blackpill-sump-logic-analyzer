cmake_minimum_required(VERSION 3.25)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-arm-none-eabi.cmake" CACHE FILEPATH "")
project(blackpill_sump C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g1 -DNDEBUG")

set(HAL_DIR  Drivers/STM32F4xx_HAL_Driver)
set(USBD_DIR Middlewares/ST/STM32_USB_Device_Library)

add_library(vendor OBJECT
  ${HAL_DIR}/Src/stm32f4xx_hal.c
  ${HAL_DIR}/Src/stm32f4xx_hal_cortex.c
  ${HAL_DIR}/Src/stm32f4xx_hal_exti.c
  ${HAL_DIR}/Src/stm32f4xx_hal_flash.c
  ${HAL_DIR}/Src/stm32f4xx_hal_flash_ex.c
  ${HAL_DIR}/Src/stm32f4xx_hal_gpio.c
  ${HAL_DIR}/Src/stm32f4xx_hal_pcd.c
  ${HAL_DIR}/Src/stm32f4xx_hal_pcd_ex.c
  ${HAL_DIR}/Src/stm32f4xx_hal_pwr.c
  ${HAL_DIR}/Src/stm32f4xx_hal_pwr_ex.c
  ${HAL_DIR}/Src/stm32f4xx_hal_rcc.c
  ${HAL_DIR}/Src/stm32f4xx_hal_rcc_ex.c
  ${HAL_DIR}/Src/stm32f4xx_ll_usb.c
  ${USBD_DIR}/Core/Src/usbd_core.c
  ${USBD_DIR}/Core/Src/usbd_ctlreq.c
  ${USBD_DIR}/Core/Src/usbd_ioreq.c
  ${USBD_DIR}/Class/CDC/Src/usbd_cdc.c
)
target_compile_definitions(vendor PUBLIC USE_HAL_DRIVER STM32F411xE)
target_include_directories(vendor PUBLIC Core/Inc USB_DEVICE/App USB_DEVICE/Target App/Inc)
# SYSTEM: vendor headers must not trigger warnings in our -Werror code.
target_include_directories(vendor SYSTEM PUBLIC
  ${HAL_DIR}/Inc ${HAL_DIR}/Inc/Legacy
  Drivers/CMSIS/Device/ST/STM32F4xx/Include Drivers/CMSIS/Include
  ${USBD_DIR}/Core/Inc ${USBD_DIR}/Class/CDC/Inc)
target_compile_options(vendor PRIVATE -w)   # third-party code: not ours to lint

add_executable(blackpill_sump.elf
  Core/Src/main.c
  Core/Src/stm32f4xx_it.c
  Core/Src/stm32f4xx_hal_msp.c
  Core/Src/system_stm32f4xx.c
  Core/Src/syscalls.c
  Core/Src/sysmem.c
  Core/Startup/startup_stm32f411ceux.s
  USB_DEVICE/App/usb_device.c
  USB_DEVICE/App/usbd_cdc_if.c
  USB_DEVICE/App/usbd_desc.c
  USB_DEVICE/Target/usbd_conf.c
  App/Src/capture.c
  App/Src/testgen.c
  App/Src/sump.c
  App/Src/usb_link.c
  App/Src/led.c
  $<TARGET_OBJECTS:vendor>)
target_link_libraries(blackpill_sump.elf PRIVATE vendor)

# Strict warnings on our own code only.
set_source_files_properties(
  App/Src/capture.c App/Src/testgen.c App/Src/sump.c App/Src/usb_link.c App/Src/led.c
  Core/Src/main.c Core/Src/stm32f4xx_it.c USB_DEVICE/App/usbd_cdc_if.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wconversion;-Wno-sign-conversion;-Werror")

target_link_options(blackpill_sump.elf PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F411CEUX_FLASH.ld
  -Wl,-Map=blackpill_sump.map
  -Wl,--start-group ${ARM_libc_nano_a} ${ARM_libm_a} ${ARM_libgcc_a} -Wl,--end-group)
set_target_properties(blackpill_sump.elf PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/STM32F411CEUX_FLASH.ld)

add_custom_command(TARGET blackpill_sump.elf POST_BUILD
  COMMAND ${OBJCOPY_EXE} -O binary $<TARGET_FILE:blackpill_sump.elf> blackpill_sump.bin
  COMMAND ${SIZE_EXE} $<TARGET_FILE:blackpill_sump.elf>
  BYPRODUCTS blackpill_sump.bin)
