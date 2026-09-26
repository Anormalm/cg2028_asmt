#ifndef FALL_DETECTOR_H
#define FALL_DETECTOR_H

#include <stdint.h>

/* Starting values for controlled trials, not calibrated performance claims.
 * Inputs are assembly-filtered acceleration (m/s^2) and angular speed (dps).
 * A posture reference is learned only during quiet normal operation, then
 * frozen during rapid rotation and candidate events.
 */
#define FD_GRAVITY              9.80665f
#define FD_LOW_ACCEL            (0.65f * FD_GRAVITY)
#define FD_IMPACT_ACCEL         (1.60f * FD_GRAVITY)
#define FD_ROTATION_DPS         100.0f
#define FD_SETTLE_MS            1000U
#define FD_IMPACT_WINDOW_MS      700U
#define FD_CONFIRM_WINDOW_MS    2500U
#define FD_ROTATION_WINDOW_MS    500U
#define FD_QUIET_MS             1000U
#define FD_POSTURE_MS            700U
#define FD_ACK_MS               1000U
#define FD_MAX_SAMPLE_GAP_MS     100U

typedef enum {
    FD_STARTUP, FD_NORMAL, FD_WAIT_IMPACT, FD_CONFIRM, FD_FALL_LATCHED
} FallState;

typedef struct {
    FallState state;
    uint32_t state_since, last_sample, rotation_at;
    uint32_t quiet_since, posture_since, button_since, reference_since;
    int has_sample, rotation_recent, rotation_seen, low_g_seen;
    int quiet_tracking, posture_tracking, button_tracking, ack_armed;
    int reference_tracking, reference_valid;
    float reference[3], reference_magnitude;
    /* Retained after an event for UART inspection, reset on next candidate. */
    float min_accel, peak_accel, peak_gyro;
    uint32_t rejection_flags, max_quiet_ms, max_posture_ms;
    const char *reason;
} FallDetector;

static void FallDetector_Init(FallDetector *d, uint32_t now)
{
    *d = (FallDetector){0};
    d->state = FD_STARTUP;
    d->state_since = now;
    d->reason = "settling";
}

static const char *FallState_Name(FallState state)
{
    switch (state) {
        case FD_STARTUP: return "STARTUP";
        case FD_NORMAL: return "NORMAL";
        case FD_WAIT_IMPACT: return "WAIT_IMPACT";
        case FD_CONFIRM: return "CONFIRM";
        case FD_FALL_LATCHED: return "FALL";
        default: return "UNKNOWN";
    }
}

static void FallDetector_Update(FallDetector *d, const float a[3],
                                float accel, float gyro, int button_pressed,
                                uint32_t now)
{
    /* A stopped debugger/missed sampling interval cannot prove continuous
     * stillness, posture, or a button hold. Preserve an already latched alarm.
     */
    if (d->has_sample && (uint32_t)(now - d->last_sample) > FD_MAX_SAMPLE_GAP_MS) {
        if (d->state == FD_FALL_LATCHED) {
            d->button_tracking = d->ack_armed = 0;
        } else {
            FallDetector_Init(d, now);
            d->reason = "sample_gap";
            d->rejection_flags = 32U;
        }
    }
    d->last_sample = now;
    d->has_sample = 1;

    if (gyro >= FD_ROTATION_DPS) {
        d->rotation_at = now;
        d->rotation_recent = 1;
    }
    if (d->rotation_recent &&
        (uint32_t)(now - d->rotation_at) > FD_ROTATION_WINDOW_MS)
        d->rotation_recent = 0;

    int quiet = accel >= 0.85f * FD_GRAVITY &&
                accel <= 1.15f * FD_GRAVITY && gyro < 20.0f;

    if (d->state == FD_STARTUP || d->state == FD_NORMAL) {
        if (quiet && !d->rotation_recent) {
            if (!d->reference_tracking) {
                d->reference_since = now;
                d->reference_tracking = 1;
            }
            if ((uint32_t)(now - d->reference_since) >= 300U) {
                for (int i = 0; i < 3; ++i) d->reference[i] = a[i];
                d->reference_magnitude = accel;
                d->reference_valid = 1;
            }
        } else {
            d->reference_tracking = 0;
        }
    }

    if (d->state == FD_WAIT_IMPACT || d->state == FD_CONFIRM) {
        if (accel < d->min_accel) d->min_accel = accel;
        if (accel > d->peak_accel) d->peak_accel = accel;
        if (gyro > d->peak_gyro) d->peak_gyro = gyro;
    }

    switch (d->state) {
        case FD_STARTUP:
            if ((uint32_t)(now - d->state_since) >= FD_SETTLE_MS) {
                d->state = FD_NORMAL;
                d->reason = "ready";
            }
            break;
        case FD_NORMAL:
            /* An impact may start a candidate without visible free fall.
             * It still needs gyro evidence AND sustained changed posture.
             */
            if (accel < FD_LOW_ACCEL || accel >= FD_IMPACT_ACCEL) {
                d->low_g_seen = accel < FD_LOW_ACCEL;
                d->rotation_seen = d->rotation_recent;
                d->quiet_tracking = d->posture_tracking = 0;
                d->rejection_flags = d->max_quiet_ms = d->max_posture_ms = 0;
                d->min_accel = d->peak_accel = accel;
                d->peak_gyro = gyro;
                d->state_since = now;
                d->state = d->low_g_seen ? FD_WAIT_IMPACT : FD_CONFIRM;
                d->reason = d->low_g_seen ? "low_g" : "direct_impact";
            }
            break;
        case FD_WAIT_IMPACT:
            if (d->rotation_recent) d->rotation_seen = 1;
            if ((uint32_t)(now - d->state_since) > FD_IMPACT_WINDOW_MS) {
                d->state = FD_STARTUP;
                d->state_since = now;
                d->reference_tracking = d->reference_valid = 0;
                d->reason = "no_impact";
                d->rejection_flags = 1U;
            } else if (accel >= FD_IMPACT_ACCEL) {
                d->state_since = now;
                d->state = FD_CONFIRM;
                d->reason = "impact";
            }
            break;
        case FD_CONFIRM: {
            if ((uint32_t)(now - d->state_since) <= 300U && d->rotation_recent)
                d->rotation_seen = 1;
            if ((uint32_t)(now - d->state_since) > FD_CONFIRM_WINDOW_MS) {
                d->rejection_flags = (!d->rotation_seen ? 2U : 0U) |
                    (!d->reference_valid ? 4U : 0U) |
                    (d->max_quiet_ms < FD_QUIET_MS ? 8U : 0U) |
                    (d->max_posture_ms < FD_POSTURE_MS ? 16U : 0U);
                d->reason = !d->rotation_seen ? "no_rotation" :
                    (!d->low_g_seen && !d->reference_valid) ? "no_reference" :
                    !d->low_g_seen && !d->max_posture_ms ? "no_posture" : "confirmation_short";
                d->state = FD_STARTUP;
                d->state_since = now;
                d->reference_tracking = d->reference_valid = 0;
                break;
            }
            if (quiet) {
                if (!d->quiet_tracking) d->quiet_since = now;
                d->quiet_tracking = 1;
                uint32_t held = (uint32_t)(now - d->quiet_since);
                if (held > d->max_quiet_ms) d->max_quiet_ms = held;
            } else d->quiet_tracking = 0;

            float dot = a[0] * d->reference[0] + a[1] * d->reference[1] +
                        a[2] * d->reference[2];
            /* cos(60 degrees)=0.5. Only treat acceleration as a gravity
             * direction near 1 g and with bounded angular movement.
             * This permits some post-fall movement rather than total stillness.
             */
            int changed_posture = d->reference_valid &&
                accel >= 0.75f * FD_GRAVITY && accel <= 1.25f * FD_GRAVITY &&
                gyro < 80.0f && dot <= 0.5f * accel * d->reference_magnitude;
            if (changed_posture) {
                if (!d->posture_tracking) d->posture_since = now;
                d->posture_tracking = 1;
                uint32_t held = (uint32_t)(now - d->posture_since);
                if (held > d->max_posture_ms) d->max_posture_ms = held;
            } else d->posture_tracking = 0;

            int still_confirmed = d->low_g_seen && d->quiet_tracking &&
                (uint32_t)(now - d->quiet_since) >= FD_QUIET_MS;
            int posture_confirmed = d->posture_tracking &&
                (uint32_t)(now - d->posture_since) >= FD_POSTURE_MS;
            if (d->rotation_seen && (still_confirmed || posture_confirmed)) {
                d->state = FD_FALL_LATCHED;
                d->state_since = now;
                d->button_tracking = d->ack_armed = 0;
                d->reason = posture_confirmed ? "posture" : "low_g_quiet";
            }
            break;
        }
        case FD_FALL_LATCHED:
            if (!button_pressed) {
                d->ack_armed = 1;
                d->button_tracking = 0;
            } else if (d->ack_armed) {
                if (!d->button_tracking) {
                    d->button_since = now;
                    d->button_tracking = 1;
                } else if ((uint32_t)(now - d->button_since) >= FD_ACK_MS) {
                    FallDetector_Init(d, now);
                    d->reason = "acknowledged";
                }
            }
            break;
    }
}

#endif
