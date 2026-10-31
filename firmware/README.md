# Firmware

STM32F411CEU6 (BlackPill). Architecture: [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md). Build and
flash with `..\tools\build.ps1` and `..\tools\flash.ps1`.

```
App/            application code (usb_link, sump, capture, testgen, led)
Core/           main, interrupts, system init, startup
USB_DEVICE/     ST CDC middleware glue and descriptors
Drivers/        STM32F4 HAL + CMSIS (vendored, unmodified)
Middlewares/    ST USB device library (vendored, unmodified)
cmake/          Clang toolchain file
blackpill.ioc   CubeMX reference for clocks/USB (do not regenerate main.c)
```

Own code builds with `-Wall -Wextra -Wshadow -Wconversion -Werror`; vendor code is compiled as-is.
