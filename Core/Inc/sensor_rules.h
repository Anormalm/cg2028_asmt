#ifndef SENSOR_RULES_H
#define SENSOR_RULES_H
#include "extra_sensors.h"
static inline int SensorConfig_Valid(const SensorConfig *c)
{
    return c->revision > 0 && c->revision <= 2147483647U &&
        c->sound <= 1 &&
        c->sound_threshold >= -800 && c->sound_threshold <= -50;
}
#endif
