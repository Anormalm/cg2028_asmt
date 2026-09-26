#ifndef ALERT_UI_H
#define ALERT_UI_H
#include <stdint.h>
typedef struct {
    uint32_t alarm_since, press_since, chirp_since;
    int alarm_active, holding, sos_armed, chirping;
} AlertUI;

/* Three-second SOS in NORMAL only. Held-at-boot or acknowledgement presses
 * cannot start another SOS until released. Detector owns the fall ACK hold.
 */
static int AlertUI_Update(AlertUI *u, uint32_t now, int pressed,
                          int normal, int alarm)
{
    if (alarm && !u->alarm_active) {
        u->alarm_since = now;
        u->chirping = 0;
    }
    if (!alarm && u->alarm_active) {
        u->chirp_since = now;
        u->chirping = 1;
    }
    u->alarm_active = alarm;
    if (!pressed) {
        u->sos_armed = 1;
        u->holding = 0;
    }
    if (alarm || !normal) {
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
        uint32_t phase = age % (age < 15000U ? 2000U : 1000U);
        return phase < 100U || (phase >= 200U && phase < 300U) ||
               (age >= 15000U && phase >= 400U && phase < 500U);
    }
    return u->chirping && (uint32_t)(now - u->chirp_since) < 100U;
}
#endif
