# Third-party components

The existing STM32 HAL, CMSIS and BSP retain their original source notices.
The optional `wifi.c/.h` and `es_wifi_io.c/.h` originated in the supplied
`Wifi(optional)` teaching resources (ST version V4.0.0, 30 November 2018).
Their original copyright/license notices are retained. The matching optional
`es_wifi.c/.h` and configuration replace the existing component at its original
path to avoid duplicate driver definitions.

`Core/RTOS` contains the required subset of the official FreeRTOS Kernel:

- Source: https://github.com/FreeRTOS/FreeRTOS-Kernel
- Tag: V11.1.0
- Commit: `dbf70559b27d39c1fdb68dfb9a32140b6a6777a0`
- Port: `portable/GCC/ARM_CM4F`
- Allocator: `portable/MemMang/heap_4.c`
- License: MIT, included in `Core/RTOS/LICENSE.md`

Kernel sources are unmodified. The application supplies `FreeRTOSConfig.h`,
HAL/RTOS SysTick integration, tasks and fault hooks.
