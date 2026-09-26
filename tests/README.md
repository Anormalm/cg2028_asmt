# Core verification

Run from the project root with Python, `unicorn`, `pyelftools`, and the STM32
ARM GCC toolchain installed:

```text
python tests/verify_core.py --toolchain "path/to/arm/toolchain/bin"
```

The script compiles the actual assembly and detector header to ARM instructions
and runs them in an emulator. It checks 32,725 EWMA inputs against an independent
integer oracle, including all alpha percentages with boundary inputs, negative
rounding, nonzero previous outputs, and callee-saved registers/stack preservation.
It also checks 14 synthetic detector scenarios, including both detection paths,
rejected movements, expired evidence, interrupted posture, tick wraparound,
sampling gaps, and button acknowledgement.

These tests do not measure real-world sensitivity or false-alarm rates. Run the
assignment's supplied assembly test project on the board as well.

## Board checks after this revision

1. Build/flash the Debug configuration. Remove motion-test breakpoints and resume.
2. Keep the board still for at least one second. Expect `NORMAL`, acceleration
   magnitude near 1000 mg, angular speed near zero, and `err=0`.
3. Check slow tilts, carrying, gentle lowering, and light shaking. Record any
   `FALL` as a false alarm; do not assume synthetic tests prove discrimination.
4. Use a protected fixture for simulated events; never perform a human fall or
   drop an exposed board. After impact, either remain quiet or maintain a changed
   orientation with limited movement. Inspect the event reasons below.
5. The latched alarm must persist during further movement. Release blue B2, then
   hold for one second to acknowledge it. A short press must not clear it.

## Event interpretation

Thresholds are collected in `Core/Inc/fall_detector.h` and remain experimental.
The vector reference assumes that the board is attached consistently to the
wearer; moving it independently does not measure the wearer's posture.

| `why` | Meaning |
| --- | --- |
| `low_g` | Acceleration below 0.65 g started a candidate |
| `direct_impact` | Impact at or above 1.6 g started a candidate without free fall |
| `impact` | Impact followed low acceleration within 700 ms |
| `no_impact` | The low-acceleration candidate timed out |
| `posture` | Rotation evidence plus >=60-degree posture change held for 700 ms |
| `low_g_quiet` | Low acceleration, impact, rotation and one second of quiet motion |
| `unconfirmed` | The candidate failed confirmation within 2.5 seconds of impact |
| `sample_gap` | More than 100 ms between samples invalidated a pending event |
| `acknowledged` | B2 hold cleared the latched alarm |

Both paths require at least 100 dps angular-speed evidence close to the event.
The direct-impact path must confirm changed posture; quiet motion alone cannot
confirm it. Posture confirmation requires acceleration within 0.75–1.25 g and
angular speed below 80 dps, allowing some movement. The low-acceleration path can
instead use stricter quiet criteria (0.85–1.15 g and below 20 dps).

Each event prints per-sample `min_mg`, `peak_mg`, and `peak_dps` measurements from
the candidate onward. These are filtered values, not unfiltered physical impact
peaks. They do not include rotation that happened before the candidate began.
Ordinary telemetry still reports raw/filtered axes, state, maximum sample interval,
and cumulative assembly/C mismatch count every 200 ms. UART remains blocking, so
watch `dtMax` for timing overruns.

Rejected candidates enter a one-second settling period before rearming. The
accelerometer remains at the BSP's +/-2 g per-axis range, which can clip impacts.
Falls without either qualifying sequence can still be missed, especially if
post-event acceleration/rotation stays high. Tune on repeated physical trials.
