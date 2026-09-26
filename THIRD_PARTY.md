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

## VL53L0X proximity driver

`Core/ToF` comes from STMicroelectronics/STM32CubeL4 commit
`3c4aaa0c009cabf2f38409cd55728d8d4d9a0cc5`, directory
`Projects/B-L475E-IOT01A/Applications/Proximity/Src/vl53l0x`.
Source: https://github.com/STMicroelectronics/STM32CubeL4/tree/3c4aaa0c009cabf2f38409cd55728d8d4d9a0cc5/Projects/B-L475E-IOT01A/Applications/Proximity/Src/vl53l0x

Original copyright notices are retained. The distribution license index is
`Core/ToF/LICENSE.md`; the ST SLA0044 terms are in `ST-license-terms.txt`.
The terms were retrieved from ST's official STM32CubeF1 `version-license.txt`,
which reproduces SLA0044: https://github.com/STMicroelectronics/STM32CubeF1/blob/master/version-license.txt
Changes in `vl53l0x_tof.c`: bounded I2C timeouts and removal of the unused
blocking single-shot demonstration function.
