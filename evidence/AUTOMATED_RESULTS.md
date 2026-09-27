# Automated assignment evidence

Generated: 2026-09-27T15:02:06.973324+00:00

These are host/emulator results, not on-board or physical-motion results.

| Check | Result | Log |
| --- | --- | --- |
| published-vectors | PASS | [published-vectors.log](published-vectors.log) |
| baseline-core | PASS | [baseline-core.log](baseline-core.log) |
| enhancement-core | PASS | [enhancement-core.log](enhancement-core.log) |
| firmware-build | PASS | [firmware-build.log](firmware-build.log) |
| oled | PASS | [oled.log](oled.log) |
| sensor-and-buzzer | PASS | [sensor-and-buzzer.log](sensor-and-buzzer.log) |
| receiver | PASS | [receiver.log](receiver.log) |
| dashboard-app-syntax | PASS | [dashboard-app-syntax.log](dashboard-app-syntax.log) |
| dashboard-sensors-syntax | PASS | [dashboard-sensors-syntax.log](dashboard-sensors-syntax.log) |

## Tested file fingerprints

Record these with physical results to identify the tested firmware. Credentials are excluded.

```text
750d52fd134d778ca94e142ef611bd6cb38a0e0e72c6d92f1b3e60f4297cbd96  CG2028_Assignment_Test/Core/Src/mov_avg.s
8f5ac23a37af844188c2f825ee8e098e935b980e442a184b89e74171739c6b43  CG2028_Assignment_TestCases.pdf
28d456ef3b625a17e79a7fe1c5a724a875dd210dd592c29d5f95b70b8472e30c  Marking_Rubric.pdf
09c93064286281765a9935e4864621224d4e45909d2b8cc79905ce14d81107f2  CG2028_Enhancements/Core/Src/alert_network.c
1e0861c4a8bd5504c0cb4af48b682b5fca244bc475e2b0e1db16f1d8f0a5ce8f  CG2028_Enhancements/Core/Src/buzzer.c
5ab07539d79770f45356ddc0fcf412039bbaacdb55610cf9a901da7e40cf695c  CG2028_Enhancements/Core/Src/extra_sensors.c
98cb4b1717284a59998a94ee8669d62ef622cb890c2070f368ed7f1130e2dec0  CG2028_Enhancements/Core/Src/main.c
3aa08cea59e342dcad1ff926d0e12a04b067ddb183b5d69da098285cf0dd95ea  CG2028_Enhancements/Core/Src/motion_capture.c
4162e71254b255bae41b98bf2bd47145c8f3e02ab12514f1a1715416952e9c11  CG2028_Enhancements/Core/Src/oled.c
18e3b1bc4980a44e76a2afeddee40cee787fe5b70c2d0411d5ae6122a4a3e4d4  CG2028_Enhancements/Core/Src/stm32l4xx_hal_msp.c
dea5f604596c5525f3f642587d788b54f8a30c5b8b57612be70e1ffa19af704d  CG2028_Enhancements/Core/Src/stm32l4xx_it.c
ebc3cdf54b06720577c3211b8c7eda0b5e312e2e6d2e128f634c9da7fde8a013  CG2028_Enhancements/Core/Src/syscalls.c
243387ccfa20a58eb25813ef29b48819cd8496a58c773af8c472f96cf29ecf5c  CG2028_Enhancements/Core/Src/sysmem.c
424de0f07335fb9641974ee4ec9cdfdbdc5df329429090fb23e40ef29eafbce4  CG2028_Enhancements/Core/Src/system_stm32l4xx.c
cd0a549e0c14096cf0a7fecf77dc884fa9f4b5185be0bcc20ce1f9522d31661e  CG2028_Enhancements/Core/Inc/fall_detector.h
1f228ec86d83b5c694ed54a6fce4af3b04fc7b3e999e530891daa1cbfd29131a  CG2028_Enhancements/Core/Inc/alert_ui.h
b11d8bd6311d415bcad47e184861a0f51b5f5a5072f9a6f73acdd7cc38cd2af1  CG2028_Enhancements/receiver/server.py
81813920129c0649de9d22497549fae58c5fec43ed3e694860a28ba6461651c2  CG2028_Enhancements/build/CG2028_Enhancements.elf
```

## Still pending

- On-board published unit tests and live assembly/C comparison.
- Repeated physical fall, normal-activity and near-fall trials.
- Measured LED timing, button operation, audible alerts and sampling under network load.
- Physical microphone response and end-to-end Wi-Fi outage/recovery.
- OLED wiring, legibility, fall/SOS/ACK screens, disconnection and recovery under load.

See ../DEMONSTRATION.md for the acceptance procedure.
