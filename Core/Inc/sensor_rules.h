#ifndef SENSOR_RULES_H
#define SENSOR_RULES_H
#include "extra_sensors.h"
static inline int SensorConfig_Valid(const SensorConfig *c)
{
    return c->revision > 0 && c->revision <= 2147483647U &&
        c->proximity <= 1 && c->sound <= 1 && c->beeps <= 1 &&
        c->near_mm >= 50 && c->near_mm <= 1000 &&
        c->far_mm >= c->near_mm + 100 && c->far_mm <= 2000 &&
        c->sound_threshold >= -800 && c->sound_threshold <= -50;
}
static inline uint32_t ProximityTone(int active, int mm, uint32_t phase, const SensorConfig *c)
{
    if (!active || !c->proximity || !c->beeps || mm < 0) return 0;
    uint32_t interval = mm <= (int)c->near_mm ? 200U :
        200U + (uint32_t)(mm-c->near_mm)*600U/(c->far_mm-c->near_mm);
    return phase % interval < 60U ? 880U : 0U;
}
#endif
