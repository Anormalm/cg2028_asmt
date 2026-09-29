# ElderCare board and dashboard user manual

Applies to `CG2028_Enhancements`, branch `integration/felix-oled`, firmware source
commit `11d0997`. This guide describes implemented behaviour. It does not establish
which firmware is currently flashed on your physical board. Build and flash this
project to run the integrated version.

The software evidence pack passed on 27 September 2026. Physical fall accuracy,
OLED operation, audible output, microphone response and timing under combined load
still require board trials. Expected outcomes below are acceptance checks, not
claims that those trials have already passed.

## 1. What is included

| Feature | What it does | Where you see it |
| --- | --- | --- |
| Motion sensing and ARM EWMA | Samples accelerometer and gyroscope, filters all six axes separately, uses filtered outputs for detection | UART and uploaded motion recordings |
| Fall detection | Combines acceleration, rotation and sustained post-event evidence | Latched alarm, UART event, remote fall event |
| LED2 | Slow blinking without a latched alarm, fast blinking during one | Board |
| Felix OLED | Smiley without a latched alarm; frown and fall/SOS text during an alarm | External SSD1306 display |
| Grove buzzer | Fall tones, Morse SOS, escalation and acknowledgement melody | External D6 buzzer |
| Manual SOS | B2 hold deliberately requests help | Local alarm and remote SOS event |
| Optional melody | Double-tap B2 to play or stop the supplied Mario-style sequence | Buzzer |
| Wi-Fi receiver | Receives events, keeps history and caregiver acknowledgements | Laptop dashboard |
| Motion recordings | Keeps raw/filtered motion around a trigger, with waveforms and CSV export | Event log → Motion recordings |
| Sound activity | Reports relative microphone level and activity bursts, with remote settings | Sensors page |

This build has no proximity measurement, distance beeps, speech recognition,
automatic emergency-service calling, SMS or email alerts. Alarm escalation after
15 seconds measures unresolved alarm age; it is not a separate diagnosis of a
long lie. The dashboard does not remotely clear the local alarm, trigger SOS or
send arbitrary text to the OLED.

## 2. Hardware setup

Power off before connecting modules. Use a data-capable USB cable to the board's
ST-LINK USB connector for programming and the virtual COM connection.

| Item | Connection / configuration |
| --- | --- |
| Base board | STM32L4S5 Discovery B-L4S5I-IOT01A |
| Grove Base Shield | Fit to Arduino headers and select **3.3 V** |
| Grove buzzer | Shield **D6**, corresponding to **PB1** |
| OLED controller | **SSD1306**, **128 x 64**, I2C address **0x3C** |
| OLED SCL | **PB8 / Arduino D15 / SCL** |
| OLED SDA | **PB9 / Arduino D14 / SDA** |
| OLED supply | **3.3 V and GND**, with I2C pull-ups to 3.3 V |
| Motion sensors and microphone | Onboard; no additional external sensor wiring |

Check labels on the actual OLED module because physical pin order varies.
Another controller, display resolution or I2C address needs driver changes.
The OLED uses I2C1, while the onboard motion sensors use I2C2. The buzzer reserves
TIM3 channel 4 on PB1.

## 3. Load the integrated firmware

1. Import `D:\CG2028\Lab3\CG2028_Enhancements` into STM32CubeIDE as an existing project.
2. Select the **CG2028_Enhancements** project and the **Debug** configuration.
3. Refresh and build the project.
4. Launch its STM32 debug configuration to program the board. If an imported
   launch points to another machine or project, create a launch for this project.
5. Resume execution after programming. A debugger paused at `main()` will not
   run the features described here.
6. Keep the board still for roughly two seconds with B2 released.

The baseline `CG2028_Assignment` and assembly unit-test project are separate.
Running them does not run the merged OLED/buzzer application.

The command-line build is available as:

```powershell
python scripts/build_firmware.py --toolchain "PATH_TO_ARM_TOOLCHAIN_BIN"
```

It produces `build/CG2028_Enhancements.elf` but does not flash it. CubeIDE normally
produces `Debug/CG2028_Enhancements.elf`.

## 4. First boot and normal operation

Open the board's actual virtual COM port at **115200 baud, 8 data bits, no parity,
1 stop bit, no flow control**. Locate the port in Windows Device Manager instead
of assuming a particular COM number. Use only one serial terminal on that port.

After startup settling, check for:

- UART `S=NORMAL` and `err=0`.
- Total acceleration `A_mg` near **1000 mg** at rest; individual axes depend on orientation.
- Angular speed `G_dps` near zero at rest.
- LED2 changing state about every **1 second**, approximately a 2-second on/off cycle.
- OLED smiley, when the display is connected and working.
- Silent buzzer unless music or a diagnostic tone is active.

The OLED smiley means **no alarm is latched**. It also appears during startup and
unconfirmed candidates; UART provides the detailed state.

The motion loop targets **50 Hz**, or one sample every **20 ms**. Each sensor axis
uses its own previous EWMA result and alpha of **50%**. A separate C implementation
checks the assembly outputs and increments `err` on a mismatch.

## 5. All B2 controls

Use the blue **B2 user button**, not the reset button.

| Starting condition | Action | Expected result |
| --- | --- | --- |
| NORMAL, B2 previously released | Hold B2 for **3 seconds** | Manual SOS alarm |
| NORMAL | **Double-tap** B2 | Start the optional melody |
| NORMAL, melody playing | Double-tap again | Stop the melody |
| Alarm active | Short tap | Alarm remains latched |
| Alarm active | **Release**, then hold B2 for **1 second** | Local acknowledgement and return through settling to NORMAL |

Each melody tap should last about **40–350 ms**; the releases must be within
**500 ms** of each other. Holding B2 while booting or continuing to hold it after
acknowledgement does not immediately trigger another SOS. Release to rearm it.

## 6. Manual SOS: the easiest first demonstration

1. Wait for UART `NORMAL`.
2. Hold B2 for three seconds.
3. Expect fast LED blinking, OLED alternating **SOS** and a frown, and Morse SOS
   from the buzzer.
4. If networking is configured, look for a **Manual SOS** event on the dashboard.
5. Leave it active for more than 15 seconds to hear escalation.
6. Release B2, then hold it for one second to acknowledge. Expect a descending
   acknowledgement melody, smiley and a return to slow blinking.

UART uses `S=FALL` for the shared alarm state, even for manual SOS. The reason
`why=manual_sos` and remote event type `sos` distinguish it from a detected fall.
An SOS demonstration proves the alarm path; it does not prove fall classification.

## 7. Automatic fall detection

The detector uses **assembly-filtered** acceleration and angular speed, not a
single raw shock threshold. Its two confirmation paths are:

| Path | Candidate sequence | Confirmation |
| --- | --- | --- |
| Low acceleration then impact | Below **0.65 g**, followed by at least **1.60 g** within **700 ms**, with rotation evidence at least **100 degrees/s** near the event | Either **1 second** of quiet motion or a sustained changed posture |
| Direct impact | At least **1.60 g**, with rotation evidence at least **100 degrees/s** near the event | Sustained changed posture is required |

Quiet confirmation requires **0.85–1.15 g** and angular speed below **20 degrees/s**.
Changed posture means at least **60 degrees** from the learned pre-event gravity
reference for **700 ms**, while acceleration is **0.75–1.25 g** and angular speed
is below **80 degrees/s**. Posture confirmation needs a valid reference.
Confirmation expires **2.5 seconds after impact**.

To rehearse, secure the board and cables in a protected fixture and use a
repeatable, cushioned simulated motion. Start from a settled orientation. Do not
perform a human fall or drop an exposed board. No specific hand movement is
guaranteed to trigger these uncalibrated thresholds.

During motion, UART may show:

| State | Meaning |
| --- | --- |
| `STARTUP` | One-second settling/rearming period |
| `NORMAL` | Monitoring, without a candidate |
| `WAIT_IMPACT` | Low acceleration occurred; waiting for qualifying impact |
| `CONFIRM` | Checking rotation and sustained post-event evidence |
| `FALL` | Latched fall or manual SOS alarm |

A rejected candidate returns to settling rather than sounding a confirmed-fall
alarm. Repeat carrying, gentle lowering, sitting-like motions, slow bending,
tilting and light shaking as negative trials. Record false alarms and missed
falls instead of selecting only successful trials.

## 8. Visual and audible indications

| Condition | OLED | LED2 | Buzzer |
| --- | --- | --- | --- |
| No latched alarm | Smiley | Slow | Silent normally |
| Confirmed fall, first 15 s | Frown / FALL DETECTED | Fast | Two short alternating tones every 2 s |
| Manual SOS, first 15 s | Frown / SOS | Fast | Repeating Morse SOS |
| Alarm unresolved after 15 s | Same alarm screens | Fast | Three rising notes every 1 s |
| Local acknowledgement | Smiley | Returns to slow | Descending three-note melody |

OLED alarm screens alternate approximately every second. Fast LED toggles target
**150 ms**, approximately a 0.30-second complete cycle; actual timing follows the
sensor-task schedule and must be measured on the board. Further movement or
quietness does not automatically clear a latched alarm.

The optional melody has **156 notes**, lasts about **37.39 seconds**, and plays
once. An alarm cancels it, and it does not automatically resume after recovery.

## 9. Configure Wi-Fi and the receiver

Local sensing, LED, OLED, buzzer and B2 controls run without a laptop receiver.
Networking requires a reachable laptop and configured credentials.

1. Use a **2.4 GHz WPA2-Personal** network reachable by both board and laptop.
   Campus enterprise authentication and captive portals are unsupported by this configuration.
2. If `Core/Inc/alert_secrets.h` already exists, edit it without overwriting its
   existing configuration. Otherwise copy `alert_secrets.example.h` to that name.
3. Set the SSID, password, laptop LAN IPv4 address, port, device ID and token.
   The current validation accepts an SSID up to 32 characters, a password up to
   32 characters and a token of 16–64 permitted characters. A generated hex token
   meets the token format. Use `ipconfig` to find the laptop's IPv4 address.
4. Rebuild and flash after changing this header.

The IP macro uses an initializer, for example `{192, 168, 1, 100}`, rather than a
quoted hostname. The default port is **8080**. `localhost` refers to the computer
using it; the board must use the laptop's LAN IP.

To generate a token if you do not already have one:

```powershell
python -c "import secrets; print(secrets.token_hex(24))"
```

Start the receiver in PowerShell:

```powershell
cd D:\CG2028\Lab3\CG2028_Enhancements
$env:ELDERCARE_TOKEN = "THE_SAME_TOKEN_AS_THE_BOARD"
python receiver/server.py --host 0.0.0.0 --port 8080
```

Keep this terminal running. Open **http://localhost:8080**, click **Settings**,
enter the same token and select **Connect**. A phone on the same reachable
LAN can use **http://LAPTOP_IP:8080**.
If Windows Firewall prompts, allow the receiver on the private demo network.
Router or hotspot client isolation can block board-to-laptop connections.

Credentials stay in the ignored `alert_secrets.h`, not in the example header.
The current protocol uses token-authenticated **HTTP**, so use a trusted private
LAN. It does not provide encrypted public-internet deployment.

## 10. Dashboard walkthrough

| Page | How to use it |
| --- | --- |
| **Overview** | Select the device, inspect its latest received status, active alerts and recent events |
| **Sound activity** | Select the board and inspect activity status/count; expand Readings and settings for levels and controls |
| **Event log** | Search/filter falls, help requests, local acknowledgements and motion recordings; open a recording for waveforms, sample inspection and CSV export |

**Mark seen** means a caregiver has acknowledged the event in the receiver. It
does **not** silence the board, clear its OLED alarm, or assert that the wearer has
recovered. B2 performs local acknowledgement.

Browser sounds and the separate Motion trials page have been removed.
Connection details are in **Settings**; connection failures still produce a visible
warning. Devices are identified by board ID; wearer profiles are not implemented.
The web interface is a received snapshot, not a guaranteed live 50 Hz motion feed.

The event log combines caregiver events from the receiver's latest **100 non-heartbeat
events** (excluding rejected candidates) with the latest **60 motion recordings**.
Entries are ordered by received time. Recordings have no incident identifier, so
they appear separately rather than being attached to a guessed alert. SQLite retains event history and saved
trial annotations across receiver restarts. Trial notes are records for engineering
analysis; they do not train a model or change fall thresholds. The normal database
is `receiver/events.db`.

## 11. Microphone demonstration and settings

1. Open **Sound activity** and select your board.
2. Observe the quiet-room reading, then speak or clap nearby.
3. Check for an activity indication and a sound event count increase.
4. Expand **Readings and settings**, inspect the relative level, set
   **Activity threshold (dBFS)** and click **Save settings**.
5. Wait for **Applied on board** before judging the new setting.

The default threshold is **−30 dBFS**. The UI allows **−80 to −5 dBFS**.
Values closer to zero are louder. Choose a threshold above the measured background
level but below the activity you want to count. For example, −40 is above −55;
these example numbers are not measurements from your board.

Distinct bursts have a two-second cooldown and quiet-release hysteresis. The
firmware masks activity while its buzzer plays and for **400 ms** afterwards.
Only levels and counts are uploaded; audio is not streamed or persistently recorded.
Disabling sound monitoring stops activity reporting, not DMA acquisition.

Settings are saved at the receiver and returned when the board contacts it.
They do not change fall thresholds. Delayed uploads or a disconnected board leave
settings pending. Sensor data becomes stale after **10 seconds** without a new
sensor upload. Delivery is not guaranteed at 5 Hz even though upload scheduling
checks a 200 ms interval.

## 12. Motion recordings in Event log

Recordings target **50 Hz**, with up to **100 samples before** and **200 samples
starting at** the trigger: about **2 seconds before and 4 seconds after**.

Automatic triggers include detector candidates and selected large raw motions.
A recording can therefore exist even when the detector rejects the motion.

For a deliberate trial:

1. Let the board settle in NORMAL for at least two seconds.
2. In CubeIDE Live Expressions, set `debug_capture_request` to **1** while running.
   The firmware consumes the request and resets it to zero.
3. Perform the planned protected motion within the following four seconds.
4. Watch `capture_completed`, then wait for `capture_uploaded` to advance.
5. Open **Event log**, filter to **Motion recordings**, and select the recording.
   Incomplete uploads show progress; waveforms appear automatically on completion.
6. Compare the raw/filtered acceleration and angular-speed waveforms. Move over or
   click a graph, or use **Inspect sample**, to read the values at a particular time.
   The red line marks the trigger. Select **Download recording CSV** for all axes.
7. Keep mounting, surface, expected and observed results in your experiment log.
   Existing labels/notes remain accessible through the capture API; authenticated
   `POST /api/notes` accepts device, boot, capture_id, label and note.

Keep the CPU running. Halting at a breakpoint invalidates timing evidence. A
full upload takes many requests and can take much longer than the six-second
recording. Metadata reports upload progress; incomplete captures cannot be exported.

Only **two recording slots** exist in board RAM. A trigger during an active
recording is ignored; no free slot increases `capture_dropped`. Wait for uploads
between trials. Board reset discards recordings still in RAM.

CSV acceleration axes use **mg**, gyro axes use **millidegrees/s**, and timestamps
use **board uptime milliseconds**. Divide gyro values by 1000 to plot degrees/s.
Clipping or sampling-gap flags need investigation. `no_candidate` means no
terminal detector decision appeared in that capture window, not proof of safety.

## 13. Network loss and recovery

The intended behaviour is that local sensing and alerts continue during receiver
or Wi-Fi failure. The sensor task has higher priority than networking and OLED.
Confirm this on hardware by watching `dtMax`, `err`, B2 and local outputs under load.

The board buffers up to **16 events** in RAM. If full, it rejects new events and
increments `dropped`. Heartbeats do not fill this queue. Retry backoff increases
from **1 to 30 seconds**; connection operations can add further delay. Retries use
the same event identity, and the receiver deduplicates them.

The Monitor device becomes offline after roughly **35 seconds** without event or
heartbeat contact. Its current physical condition is then unknown. The Sensors
page has a separate, shorter staleness timer.

Rehearsal: start from normal operation, stop the receiver, trigger SOS, observe
local alerts, then restart the receiver with the same token. Expect the pending
event once after delivery succeeds. Keep the board powered during this test.
Reset clears its unsent queue, captures and runtime alarm state; it is not the
normal acknowledgement procedure.

## 14. Read the diagnostics

| Field / expression | Meaning |
| --- | --- |
| UART `S` | Detailed state |
| `err` | Cumulative assembly/C mismatches; expected zero |
| `dtMax` | Largest interval between samples since the last UART summary |
| `Ar` / `Af` | Raw / filtered acceleration axes, mg |
| `Gr` / `Gf` | Raw / filtered gyro axes, millidegrees/s |
| `A_mg` / `G_dps` | Filtered vector magnitudes |
| `net=0` | Networking disabled by invalid/missing configuration or boot RNG failure |
| `net=1` | Connecting or waiting for first successful delivery |
| `net=2` | Last application-level delivery succeeded |
| `net=3` | Connection/delivery failed and is retrying |
| `delivered` | Last event/heartbeat sequence acknowledged by receiver |
| `dropped` | Events lost because board queue was full |
| `oled_online` | Last display initialization/transfer status, 1 for success |
| `oled_errors` | Count of display transfer/initialization failures |
| `buzzer_frequency_hz` | Requested PWM pitch, 0 means silence |
| `capture_completed/uploaded/dropped` | Recording completion, upload and full-slot counters |
| `network_step/network_failed_step` | Current / most recent failed network operation |
| `network_last_status/network_http_status` | Driver/protocol result and received HTTP status |

An unchanged normal OLED screen is not continuously resent, so `oled_online=1`
is not a continuous cable-presence test. Similarly, `net=2` reflects the last
successful operation rather than a continuously verified Wi-Fi link.

Candidate rejection reasons include `no_impact`, `no_rotation`, `no_reference`,
`no_posture`, `confirmation_short` and `sample_gap`. Use these with recorded traces.
A sample gap over **100 ms** resets pending detection, while a latched alarm
survives and its acknowledgement must be rearmed.

## 15. Troubleshooting

| Symptom | Check |
| --- | --- |
| Nothing runs after programming | Correct enhancement project/ELF, successful flash and debugger Resume |
| Blank UART | ST-LINK data cable, actual COM port, 115200 8-N-1, no second terminal occupying the port |
| Wrong or old behaviour | Refresh/rebuild/flash `CG2028_Enhancements`, not the baseline or test project |
| Blank OLED | Controller/address, 3.3 V power, SDA/SCL, `oled_online` and `oled_errors`; retry occurs about 5 s after failure |
| Silent buzzer | D6 connection, shield 3.3 V, UART NORMAL, deliberate double-tap; inspect `buzzer_frequency_hz` |
| Buzzer holds one tone during debugging | CPU may be halted while hardware PWM continues; Resume |
| SOS does not trigger | Wait for NORMAL, release B2, then hold continuously for 3 s |
| Alarm does not clear | Release B2 first, then hold for 1 s; a short tap and dashboard Mark seen do not clear it |
| Dashboard has no device | Receiver running, correct LAN IP/token/port, credentials flashed, same reachable network, firewall/client isolation |
| Website shows old UI | Restart the updated receiver and hard-refresh the browser with Ctrl+F5 |
| Microphone settings remain pending | Board needs successful sensor communication; wait for Applied on board |
| Sound count ignores the buzzer | Expected masking behaviour, including 400 ms afterwards |
| Recording missing or incomplete | Capture/upload counters, free RAM slots and network progress; do not reset the board |
| Err counter increases | Investigate assembly/C agreement before relying on the demo |
| Repeated sample gaps | Remove breakpoints and investigate timing/load rather than treating gaps as valid stillness |
| Gentle motion triggers FALL | Record the false alarm and inspect evidence; thresholds need physical tuning |
| Planned fall motion is missed | Record it and inspect the candidate, rotation, posture/quiet evidence and clipping |

Optional buzzer diagnostic: in NORMAL, set `debug_buzzer_test_hz=523` in Live
Expressions. It plays for one second and clears the request. Repeat with 784 and
1047 after each completes. Accepted requests are 100–5000 Hz, and alarms preempt
the diagnostic. Do not halt the CPU while listening.

## 16. Suggested complete demonstration order

1. Show the exact assembly test result and normal UART `err=0`.
2. Show the smiley, slow LED and changing raw/filtered motion readings.
3. Perform rehearsed normal/near-fall activities and record any false alarms.
4. Perform a protected fall trial and show confirmed FALL, OLED and fast LED.
5. Let the buzzer escalate after 15 s; demonstrate that a short tap does not clear it.
6. Release and hold B2 for 1 s to acknowledge locally.
7. Trigger manual SOS and show its distinct OLED text, sound and dashboard event.
8. Click Mark seen and show that the local alarm remains until B2 acknowledgement.
9. Demonstrate microphone level, changed settings becoming applied and buzzer masking.
10. Open a motion recording in Event log, inspect its waveforms and export its CSV, including actual failed trials.
11. Demonstrate receiver outage/recovery while local functions continue.
12. Optionally play/stop the melody as a final interaction demonstration.

Do not report physical detection rates or timing until measured. The existing
software passes include five published EWMA cases (240 sequential axis outputs),
32,725 additional ARM EWMA cases, 14 synthetic detector scenarios, built OLED/PWM/
microphone checks, ten receiver tests and dashboard syntax checks. These do not
represent hidden test results or measured real-world accuracy.

Further references: [README](README.md), [demonstration/evidence guide](DEMONSTRATION.md),
[automated results](evidence/AUTOMATED_RESULTS.md), [physical trial record](evidence/PHYSICAL_TRIALS.md),
[microphone details](SENSORS.md).
