# Proximity and sound

Build and flash the enhancement project in CubeIDE, resume execution, then open
http://localhost:8080 and select **Sensors**. Refresh an already open page.
Select the board to see distance, proximity state, relative sound level, burst
count and recent charts. No Live Expression edits are needed.

Enable **Proximity beeps** and click **Save settings** to try distance feedback.
Beeps default off; near/far boundaries default to 300/800 mm. Two fresh readings
inside the far boundary activate feedback; a 50 mm release margin prevents
chatter. Pulses accelerate from roughly every 800 ms to every 200 ms as a target
approaches. Alarms, acknowledgement, pitch tests and music take priority,
including their silent gaps. Keep the onboard VL53L0X window clear of the shield
and wires. Start with a flat target 20-80 cm away. Invalid or stale readings show
unavailable; range is capped at 2 m and depends on target and lighting.

The sound threshold defaults to -30 dBFS. Observe the quiet-room level, then
clap or speak nearby and set a threshold above the background. dBFS is relative
digital level, not calibrated sound pressure or speech recognition. Separate
bursts have a two-second cooldown and quiet-release hysteresis. The buzzer and
its 400 ms tail suppress activity detection. Only levels and counts are sent;
transient microphone samples stay in RAM. Disabling monitoring stops activity
reporting, not the underlying DMA acquisition.

The website saves settings in the receiver database and shows pending versus
applied revisions. Uploads are attempted roughly once per second, subject to
Wi-Fi latency, alarms and retries. Readings become offline after ten seconds.
Charts show up to 120 observations; storage retains up to 1800 per device.
Caregiver acknowledgement does not silence the board or confirm recovery.

Before the showcase, check distance against a ruler, sound response, saved
settings becoming applied, and alarm priority with both sensors and Wi-Fi
running. Check `dtMax` for sampling delays. Sensor initialization errors require
correcting the hardware issue and resetting. Fall thresholds are unchanged.

After building, run `python tests/verify_sensor_firmware.py`. It executes built
ARM code with synthetic microphone frames and mocked physical range calls:
RMS/DC removal, buzzer masking, burst counts, dropouts, fragmented configuration
replies, fresh-range confirmation, hysteresis, cadence and invalid/stale ranges.
RTOS critical sections are bypassed for single-threaded emulation; interrupt
timing, physical I2C and DMA wiring require the hardware checks above.

Receiver tests also cover sensor authentication, persistence, desired/applied
revisions, validation, retries, staleness and old-boot protection.
