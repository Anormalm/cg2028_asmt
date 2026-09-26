#ifndef ALERT_UI_H
#define ALERT_UI_H
#include <stdint.h>
typedef struct {
    uint32_t alarm_since, press_since, chirp_since, tap_since, tune_since;
    int alarm_active, holding, sos_armed, chirping, tap_pending, tuning, sos_pattern;
} AlertUI;

/* Three-second SOS in NORMAL only. Held-at-boot or acknowledgement presses
 * cannot start another SOS until released. Detector owns the fall ACK hold.
 */
static int AlertUI_Update(AlertUI *u, uint32_t now, int pressed,
                          int normal, int alarm)
{
    if ((uint32_t)(now - u->tune_since) >= 800U) u->tuning = 0;
    if ((uint32_t)(now - u->chirp_since) >= 100U) u->chirping = 0;
    if (alarm && !u->alarm_active) {
        u->alarm_since = now;
        u->chirping = 0;
        u->tuning = u->tap_pending = u->sos_pattern = 0;
    }
    if (!alarm && u->alarm_active) {
        u->chirp_since = now;
        u->chirping = 1;
    }
    u->alarm_active = alarm;
    if (!pressed) {
        /* Two deliberate short taps in NORMAL play a short rhythm test. */
        uint32_t held = (uint32_t)(now - u->press_since);
        if (normal && !alarm && u->holding && held >= 40U && held <= 350U) {
            if (u->tap_pending && (uint32_t)(now - u->tap_since) <= 500U) {
                u->tune_since = now;
                u->tuning = 1;
                u->tap_pending = 0;
            } else { u->tap_since = now; u->tap_pending = 1; }
        }
        u->sos_armed = 1;
        u->holding = 0;
    }
    if (alarm || !normal) {
        u->tap_pending = 0;
        u->holding = 0;
        if (pressed) u->sos_armed = 0;
    } else if (pressed && u->sos_armed) {
        if (!u->holding) { u->press_since = now; u->holding = 1; }
        if ((uint32_t)(now - u->press_since) >= 3000U) {
            u->holding = u->sos_armed = 0;
            return 1;
        }
    }
    return 0;
}

static int AlertUI_BuzzerOn(const AlertUI *u, uint32_t now)
{
    if (u->alarm_active) {
        uint32_t age = (uint32_t)(now - u->alarm_since);
        if (u->sos_pattern && age < 15000U) {
            /* Morse SOS: three dots, three dashes, three dots, then a pause. */
            uint32_t phase = age % 4000U;
            static const uint16_t starts[9] = {0,200,400,800,1200,1600,2200,2400,2600};
            for (int i = 0; i < 9; ++i)
                if (phase >= starts[i] && phase < starts[i] + (i >= 3 && i <= 5 ? 300U : 100U)) return 1;
            return 0;
        }
        uint32_t phase = age % (age < 15000U ? 2000U : 1000U);
        return phase < 100U || (phase >= 200U && phase < 300U) ||
               (age >= 15000U && phase >= 400U && phase < 500U);
    }
    if (u->chirping && (uint32_t)(now - u->chirp_since) < 100U) return 1;
    if (u->tuning) {
        uint32_t phase = (uint32_t)(now - u->tune_since);
        return phase < 80U || (phase >= 160U && phase < 240U) ||
               (phase >= 320U && phase < 560U) || (phase >= 720U && phase < 800U);
    }
    return 0;
}
#endif
