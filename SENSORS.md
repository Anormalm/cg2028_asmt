# Microphone sound activity

The current firmware implements microphone sound-level/activity monitoring.
It does not implement proximity measurement or distance beeps.

Build and flash the enhancement project in CubeIDE, resume execution, then open
the receiver dashboard and select **Sensors**. Select the board to inspect the
relative sound level, burst count, microphone status and recent history.

The sound threshold defaults to -30 dBFS. Observe the quiet-room level, then
clap or speak nearby and set a threshold above the background. dBFS is relative
digital level, not calibrated sound pressure or speech recognition. Separate
bursts have a two-second cooldown and quiet-release hysteresis. Buzzer playback
and its 400 ms tail mask activity detection. Only levels and counts are sent;
transient microphone samples stay in RAM. Disabling monitoring stops activity
reporting, not the underlying DMA acquisition.

The website saves settings in the receiver database and shows desired versus
applied revisions. The network task checks whether a sensor upload is due after
200 ms, with alarms and due heartbeats taking priority and capture uploads
interleaved. This is not a guaranteed 5 Hz delivery rate: Wi-Fi latency and retries
affect the observed rate. Sensor readings become offline after ten seconds.
Charts show up to 120 observations; storage retains up to 1800 per device.

Before the demonstration, verify physical sound response, settings becoming
applied, buzzer masking and dtMax while networking and motion recording are
active. Hardware initialization errors require resolving the cause and resetting.
Fall thresholds are unchanged by sound monitoring.

After building, run `python tests/verify_sensor_firmware.py`. It checks the built
ARM PWM driver and sensor processing with synthetic DMA frames: RMS/DC removal,
buzzer masking, burst counts, dropouts and fragmented configuration replies.
RTOS critical sections are bypassed for single-threaded emulation. Physical
microphone wiring, interrupts and timing still need hardware verification.

Receiver tests cover sensor authentication, persistence, settings revisions,
validation, retries, staleness and old-boot protection. See
`../DEMONSTRATION.md` for the full assignment evidence and physical trial plan.
