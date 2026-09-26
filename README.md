# ElderCare: buzzer and Wi-Fi alerts

Branch: `feature/buzzer-wifi-alerts`. This project includes the latest detector
and overflow-safe assembly filter, a Grove buzzer, manual SOS, and a Wi-Fi
receiver/dashboard. The OLED is not part of this branch.

## 1. Connect the buzzer

Power off first. Fit the Grove Base Shield to the board's Arduino headers, set
its voltage selector to **3.3 V**, and connect the standard **Grove Buzzer to D6**.
D6 maps to **PB1** on B-L4S5I-IOT01A. Do not use an I2C or UART Grove connector.
A passive buzzer requires a PWM implementation and is not supported by this code.

## 2. Configure Wi-Fi

The firmware builds without credentials and runs the local features with
`net=0`. To enable remote alerts:

1. Copy `Core/Inc/alert_secrets.example.h` to `Core/Inc/alert_secrets.h`.
2. Enter a **2.4 GHz WPA2-Personal** SSID and password. This version validates
   both as at most 32 characters, matching the supplied driver configuration.
   Campus enterprise Wi-Fi and captive portals are not supported.
3. Set `ALERT_SERVER_IP` to the laptop's LAN IPv4 address (see `ipconfig`).
4. Set `ALERT_TOKEN` to a random 16–64-character token with no spaces, using the
   same value on the receiver. Generate one with:

   ```powershell
   python -c "import secrets; print(secrets.token_hex(24))"
   ```

5. Use a distinct `ALERT_DEVICE_ID` for each board (letters, digits, `_` or `-`).

`alert_secrets.h` is excluded from Git. Do not put credentials in the example
header or post them in UART logs. The protocol uses HTTP with a bearer token:
**use a trusted private LAN for this demo**. The token and event data are not
encrypted. Public deployment would need TLS and additional access controls.

## 3. Start the laptop receiver

Python 3.10+ is sufficient; the receiver has no third-party Python dependencies.
In PowerShell, from this project directory:

```powershell
$env:ELDERCARE_TOKEN = "YOUR_GENERATED_TOKEN"
python receiver/server.py --host 0.0.0.0 --port 8080
```

If prompted by Windows Firewall, allow Python on the private network used for
the demo. The board and laptop must be able to reach each other; hotspot or
router client isolation can prevent this even when both are connected.

Open **http://localhost:8080** on the laptop. Enter the token and select Connect.
On a phone on the same reachable LAN, use `http://LAPTOP_IP:8080`.
The dashboard token is kept in browser session storage, not in the URL.

The receiver saves events in `receiver/events.db` (SQLite, ignored by Git),
commits each event before replying with its exact delivery ACK, and deduplicates
retries by device ID, random boot ID, and sequence number. It retains history
across receiver restarts. **Mark seen** records caregiver acknowledgement on the
receiver only; it does not silence the board or assert that the wearer recovered.

## 4. Build and flash

Import this folder in STM32CubeIDE as an existing project. Its project name is
**CG2028_Enhancements**, so it can coexist with the original assignment project.
Select **Debug**, refresh the project, build, and debug/resume as usual. Create a
new STM32 debug launch if the imported launch retains machine-specific paths.
The ELF is `Debug/CG2028_Enhancements.elf` for a CubeIDE build.

Alternatively, build using the installed ARM toolchain without launching the IDE:

```powershell
python scripts/build_firmware.py --toolchain "PATH_TO_ARM_TOOLCHAIN_BIN"
```

This produces `build/CG2028_Enhancements.elf`. It does not flash the board.
The command-line build and CubeIDE use the same application/vendor sources.

## 5. Demonstrate the behaviour

Open UART at **115200, 8-N-1, no flow control**. After startup settling, expect
`NORMAL`, `err=0`, acceleration near 1000 mg, and a slow LED blink.

| Action | Expected behaviour |
| --- | --- |
| Normal operation | Silent buzzer, slow LED, heartbeat about every 10 s |
| Hold blue B2 for 3 s in NORMAL | Manual SOS, fast LED, audible alarm, queued SOS event |
| Confirmed simulated fall | Fast LED, audible alarm, queued fall event |
| Double-tap B2 in NORMAL | Rising C5-E5-G5-C6 melody |
| First 15 s of a fall alarm | Alternating 1568/2093 Hz, two 100 ms notes every 2 s |
| First 15 s of manual SOS | Morse SOS at 2093 Hz: three short, three long, three short |
| Alarm unresolved after 15 s | Three rising notes (1568/2093/2637 Hz) every 1 s |
| Release B2, then hold 1 s during an alarm | Local acknowledgement, descending C7-G6-C6 melody, settling then NORMAL |
| Short B2 tap during alarm | Alarm remains latched |
| Caregiver clicks Mark seen | Dashboard records it; board alarm remains active |
| Stop receiver or disconnect Wi-Fi | Local sensing/LED/buzzer/button continue; delivery retries |
| Restore connectivity | Pending events delivered in order; retries appear only once |
| No messages for 35 s | Dashboard marks the device OFFLINE |

Use **manual SOS first** to test networking without tuning fall detection.
Then use controlled motions with a protected fixture. Do not perform human falls
or drop an exposed board. See `tests/README.md` for detector testing and thresholds.

Extra UART fields:

- `net=0`: disabled (credentials missing/invalid or boot RNG setup failed).
- `net=1`: initializing/connecting or waiting for the first successful delivery.
- `net=2`: last application-level delivery succeeded.
- `net=3`: connection or delivery failed; backing off before retry.
- `delivered`: last sequence explicitly acknowledged by the receiver.
- `dropped`: events rejected because the RAM queue was full.
- `dtMax`: largest measured sample interval since the previous UART summary.

Connection state reflects the last operation, not a continuously verified link.
Queued event timestamps use board uptime; the dashboard also displays laptop
receipt time. Offline events can arrive later than their actual occurrence.

## Design and limits

- **Sensor task priority 3:** sampling, filter, detector, B2, LED, buzzer and UART.
- **Network task priority 1:** the sole caller of Wi-Fi/SPI APIs, HTTP delivery and retries.
- FreeRTOS preemption keeps network waits out of the sensor task. SPI readiness
  waits/delays yield; SPI transfers have finite timeouts. Neither task uses the
  other task's peripherals. UART is still blocking within the sensor task: verify
  `dtMax` during the demo, especially on state transitions.
- CPU runs at 80 MHz from the MSI PLL; Wi-Fi uses SPI3 at 10 MHz. The board's
  onboard sensor bus and Grove D6 are separate from SPI3.
- Up to **16 events** are retained in RAM. When full, new events are dropped and
  the count increases; the oldest undelivered events remain queued. Heartbeats
  are generated separately and do not fill the queue. **Board reset/power loss
  clears pending events**; this version does not claim persistent offline storage.
- Retry delay grows from 1 to 30 s. Driver connect operations can themselves
  wait up to 30 s in the network task. Response parsing handles split TCP replies.
- The hardware RNG provides a 64-bit boot nonce to prevent ID reuse after reset.
- Manual SOS shares the latched alarm/acknowledgement mechanism; UART state is
  `FALL`, with `why=manual_sos` and a distinct `sos` remote event.
- The supplied Wi-Fi layer was integrated once, with bounds checks added to reset
  and receive handling. Do not add a second `es_wifi.c` to the project.
- The optional OLED should use its own driver. Do not call Wi-Fi APIs from its
  task, and do not access the onboard sensor I2C bus concurrently without a mutex.

## Verification

```powershell
python -m unittest discover -s tests -p test_receiver.py -v
python tests/verify_core.py --toolchain "PATH_TO_ARM_TOOLCHAIN_BIN"
```

The emulator tests require `unicorn` and `pyelftools`. They execute the actual ARM
assembly and compiled detector/UI/protocol logic. The receiver tests use a real
loopback HTTP server and temporary SQLite databases. They cover authentication,
duplicate/conflicting IDs, history persistence, offline state, caregiver ACK,
and late events. These checks do **not** replace on-board testing of the SPI
module, Wi-Fi association, D6 output, task scheduling or calibrated fall accuracy.

FreeRTOS licensing/provenance is recorded in `THIRD_PARTY.md`.

## Motion recordings and the updated dashboard

After updating this branch, restart `receiver/server.py` using your existing
token and database, then hard-refresh the website (Ctrl+F5). Existing events are
preserved; the receiver adds recording tables automatically. Refresh the
**CG2028_Enhancements** project in CubeIDE, rebuild, launch Debug and Resume.
The original assignment project is a separate folder.

The dashboard has Monitor, Motion trials and Event log views. It supports device
and event filters, incident details, caregiver acknowledgement, optional browser
alert sounds, and CSV exports. Browser sounds require an explicit click and are
separate from the board buzzer. Recording plots compare raw and assembly-filtered
acceleration/angular-speed magnitudes; the downloadable CSV preserves all axes.
Trial labels and notes persist in SQLite. Labels record your observations and
do not automatically alter the detector.

Recordings target 50 Hz: up to 100 pre-trigger samples and 200 samples starting
at the trigger (approximately two seconds before and four seconds after).
Triggers include a detector candidate or raw acceleration below 500 mg / above
1800 mg, or raw angular speed above 150 degrees/s, while NORMAL. Raw triggers
help capture motion that filtering prevented from becoming a candidate. The
automatic trigger rearms after 50 consecutive quiet samples. Thresholds for
fall detection have not been calibrated by these changes.

In CubeIDE **Live Expressions**, use these variable names:

| Expression | Meaning |
| --- | --- |
| `debug_capture_request` | Set to `1` to start a manual recording; firmware resets it to `0` |
| `capture_completed` | Completed recordings since boot |
| `capture_uploaded` | Recordings fully acknowledged by the receiver |
| `capture_dropped` | Trigger requests rejected because both RAM slots were full |
| `network_step` | Current network operation |
| `network_failed_step` | Most recent failed operation |
| `network_last_status` | Last driver's failure status; `-1` also indicates an invalid/missing HTTP ACK |
| `network_http_status` | HTTP response status, when one was received |

Only **two recordings** fit in the recorder's allocated RAM slots. A trigger
during an existing recording is ignored; inspect the counters and wait for an
upload before another trial. Board reset discards RAM recordings. Each full
recording uses 100 three-sample HTTP requests, so uploading can take considerably
longer than recording. Alarms and due heartbeats have priority between requests;
a request already in progress completes or times out first. Partial recordings
show upload progress and are not offered as complete plots/CSV files.

Rejected candidates now report `no_impact`, `no_rotation`, `no_reference`,
`no_posture`, `confirmation_short`, or `sample_gap`. UART `reject` is a bitmask:
1=no impact, 2=no rotation, 4=no reference, 8=quiet hold too short,
16=posture hold too short, 32=sampling gap. The confirmation paths are alternatives,
so not every check is required on every path. Capture flags separately encode
low-g(1), rotation(2), reference(4), quiet(8), posture(16), possible axis clipping(32)
and sampling gap(64). Gyroscope CSV axes are in **millidegrees/s**, while plotted
magnitudes use **degrees/s**. Acceleration axes use **mg**. All timestamps in CSV
are unsigned board uptime milliseconds.

The sensor's current ±2g range can clip hard impacts. The dashboard flags axes
approaching ±1950 mg; it cannot reconstruct a clipped peak. A recording marked
`no_candidate` means no terminal detector decision was captured in that window,
not proof that the motion was safe.

## Buzzer sound design

D6/PB1 now uses **TIM3 channel 4, alternate function AF2**, as specified in
[ST's STM32L4S5 datasheet, Table 16](https://www.st.com/resource/en/datasheet/stm32l4s5qi.pdf).
Keep the Grove Buzzer on D6 and the shield at 3.3 V. No rewiring is required.
The timer generates approximately 50% duty PWM using a 1 MHz counter, with
rounded periods for each requested pitch. TIM3 is reserved for the buzzer;
do not let another enhancement reconfigure this timer or PB1.

Two short B2 taps (each 40-350 ms, releases within 500 ms) play a rising
C5-E5-G5-C6 motif (523, 659, 784, 1047 Hz). The first three notes last
220 ms with 40 ms rests; the final note lasts 400 ms. This replaces the earlier
higher, uneven chirp-like test sequence. Manual SOS retains its Morse rhythm
at 2093 Hz; fall alarms alternate 1568/2093 Hz. Both escalate after 15 seconds to
three rising notes every second. Local acknowledgement plays C7-G6-C6 over
340 ms, including rests. Alarm onset cancels a test melody immediately on the
next sensor update. Holding B2 retains its SOS/acknowledgement behaviour.

`Core/Src/buzzer.c` programs TIM3 directly using the supplied CMSIS/HAL headers;
it needs no timer interrupt, DMA or extra HAL TIM source. The sensor task selects
notes every 20 ms, but the hardware generates each audio cycle independently.
Repeated requests for the same pitch do not restart the waveform. Silence sets
zero duty and stops the counter; the fatal-error handler also stops the buzzer.

Add `buzzer_frequency_hz` to CubeIDE Live Expressions to inspect the requested
pitch (`0` means silent). It is a diagnostic, not a control variable. Actual pitch
is slightly rounded by the timer period. While the debugger halts the CPU, the
last PWM tone can continue because the sequencing task is paused; Resume to
continue the sequence. Test timing with the CPU running, not single-stepping.

Seeed documents PWM tone control for the
[Grove Buzzer](https://wiki.seeedstudio.com/Grove-Buzzer/). Audible pitch and volume
still require verification on the actual module. Build and flash this firmware,
then double-tap B2 in NORMAL before testing SOS, escalation and acknowledgement.


### Diagnosing the sound in CubeIDE

While the board is running in NORMAL, set `debug_buzzer_test_hz` to `523`.
It plays that pitch for one second and resets the request to zero. Repeat with
`784` and `1047`, waiting for each tone to finish. Read `buzzer_frequency_hz` to
see the current requested output. Requests outside 100-5000 Hz or outside NORMAL
are ignored; an alarm immediately preempts the test. Do not pause the CPU while
listening, since debugger halts also pause the one-second timeout.

Different pitches that sound harsh suggest the note range/timbre needs adjustment;
identical pitches, severe distortion or uneven sustained tones need further
hardware/timing checks. A lower melody is an acoustic tuning attempt, not proof
that the actual sound has been verified remotely. The alarm pitches are unchanged.
