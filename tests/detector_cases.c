/* Synthetic FILTERED inputs: state-machine regression, not sensor calibration. */
#include "fall_detector.h"

static FallDetector d;
static uint32_t t;

static void sample(float g, float speed, int sideways, int pressed)
{
    float a[3] = {0.0f, 0.0f, 0.0f};
    a[sideways ? 0 : 2] = g * FD_GRAVITY;
    t += 20U;
    FallDetector_Update(&d, a, g * FD_GRAVITY, speed, pressed, t);
}

static void run(int n, float g, float speed, int sideways, int pressed)
{
    for (int i = 0; i < n; ++i) sample(g, speed, sideways, pressed);
}

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int test_entry(int scenario)
{
    t = scenario == 10 ? UINT32_MAX - 1500U : 0;
    FallDetector_Init(&d, t);
    run(60, 1.0f, 0.0f, 0, 0);
    CHECK(d.state == FD_NORMAL && d.reference_valid);
    CHECK(FallState_Name(d.state)[0] == 'N');

    switch (scenario) {
        case 0: /* Stationary and slow posture changes do not initiate a fall. */
            run(100, 1.0f, 10.0f, 1, 0);
            CHECK(d.state == FD_NORMAL);
            break;
        case 1: /* Conventional unloading, impact, rotation, quiet sequence. */
        case 10: /* The same sequence straddles HAL tick wraparound. */
            sample(0.4f, 150.0f, 0, 0);
            CHECK(d.state == FD_WAIT_IMPACT);
            sample(1.8f, 150.0f, 0, 0);
            CHECK(d.state == FD_CONFIRM);
            run(55, 1.0f, 0.0f, 0, 0);
            CHECK(d.state == FD_FALL_LATCHED);
            break;
        case 2: /* No free fall; posture can confirm despite moderate motion. */
            sample(1.8f, 150.0f, 0, 0);
            run(40, 1.0f, 40.0f, 1, 0);
            CHECK(d.state == FD_FALL_LATCHED);
            break;
        case 3: /* Impact + rotation alone, upright afterwards: reject. */
            sample(1.8f, 150.0f, 0, 0);
            run(130, 1.0f, 0.0f, 0, 0);
            CHECK(d.state == FD_STARTUP);
            CHECK(d.rejection_flags == 16U && d.max_quiet_ms >= FD_QUIET_MS);
            break;
        case 4: /* Even changed posture must have gyro evidence. */
            sample(1.8f, 0.0f, 0, 0);
            run(130, 1.0f, 0.0f, 1, 0);
            CHECK(d.state == FD_STARTUP);
            CHECK(d.rejection_flags == 2U && d.reason[3] == 'r');
            break;
        case 5: /* Rotation preceding impact is retained briefly. */
            sample(1.0f, 150.0f, 0, 0);
            run(10, 1.0f, 0.0f, 0, 0);
            sample(1.8f, 0.0f, 0, 0);
            run(40, 1.0f, 40.0f, 1, 0);
            CHECK(d.state == FD_FALL_LATCHED);
            break;
        case 6: /* Old rotation must not validate a later unrelated impact. */
            sample(1.0f, 150.0f, 0, 0);
            run(30, 1.0f, 0.0f, 0, 0);
            sample(1.8f, 0.0f, 0, 0);
            run(130, 1.0f, 0.0f, 1, 0);
            CHECK(d.state == FD_STARTUP);
            break;
        case 7: /* Free fall without impact expires. */
            sample(0.4f, 150.0f, 0, 0);
            run(36, 1.0f, 0.0f, 0, 0);
            CHECK(d.state == FD_STARTUP);
            CHECK(d.rejection_flags == 1U);
            break;
        case 8: /* Posture timer resets when original posture returns. */
            sample(1.8f, 150.0f, 0, 0);
            run(25, 1.0f, 40.0f, 1, 0);
            sample(1.0f, 0.0f, 0, 0);
            run(25, 1.0f, 40.0f, 1, 0);
            CHECK(d.state == FD_CONFIRM);
            run(12, 1.0f, 40.0f, 1, 0);
            CHECK(d.state == FD_FALL_LATCHED);
            break;
        case 9: /* Missing samples invalidate a pending confirmation. */
            sample(1.8f, 150.0f, 0, 0);
            run(25, 1.0f, 40.0f, 1, 0);
            t += 1000U;
            sample(1.0f, 0.0f, 1, 0);
            CHECK(d.state == FD_STARTUP);
            CHECK(d.rejection_flags == 32U);
            break;
        case 11: /* Held button, short tap, sampling gap, then valid hold. */
            sample(1.8f, 150.0f, 0, 1);
            run(40, 1.0f, 40.0f, 1, 1);
            CHECK(d.state == FD_FALL_LATCHED);
            run(60, 1.0f, 0.0f, 1, 1);
            CHECK(d.state == FD_FALL_LATCHED);
            sample(1.0f, 0.0f, 1, 0);
            run(10, 1.0f, 0.0f, 1, 1);
            sample(1.0f, 0.0f, 1, 0);
            CHECK(d.state == FD_FALL_LATCHED);
            run(25, 1.0f, 0.0f, 1, 1);
            t += 2000U;
            sample(1.0f, 0.0f, 1, 1);
            CHECK(d.state == FD_FALL_LATCHED);
            sample(1.0f, 0.0f, 1, 0);
            run(51, 1.0f, 0.0f, 1, 1);
            CHECK(d.state == FD_STARTUP);
            break;
        case 12: /* No stable pre-event reference: direct-impact path rejects. */
            d.reference_valid = 0;
            sample(1.8f, 150.0f, 0, 0);
            run(130, 1.0f, 40.0f, 1, 0);
            CHECK(d.state == FD_STARTUP);
            break;
        case 13: /* Repeated shaking without sustained posture rejects. */
            sample(1.8f, 150.0f, 0, 0);
            for (int i = 0; i < 125; ++i)
                sample(i % 2 ? 1.8f : 0.5f, 150.0f, i % 2, 0);
            sample(1.0f, 0.0f, 0, 0);
            CHECK(d.state == FD_STARTUP);
            break;
        default: return -1;
    }
    return 0;
}
