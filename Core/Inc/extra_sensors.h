#ifndef EXTRA_SENSORS_H
#define EXTRA_SENSORS_H
#include <stdint.h>
typedef struct {
    uint32_t revision;
    unsigned proximity, sound, beeps, near_mm, far_mm;
    int sound_threshold; /* Tenths of dB relative to digital full scale, not SPL. */
} SensorConfig;
typedef struct {
    uint32_t uptime_ms, config_revision, sound_events, audio_overruns;
    int distance_mm, range_status, proximity_active;
    int sound_dbfs, sound_valid, sound_active, sound_masked, mic_error;
} SensorSnapshot;
void ExtraSensors_Init(void);
void ExtraSensors_Update(uint32_t now);
uint32_t ExtraSensors_ProximityTone(uint32_t now);
void ExtraSensors_Get(SensorSnapshot *snapshot);
void ExtraSensors_Configure(const SensorConfig *config);
int ExtraSensors_ParseReply(const char *response, const char *expected);
#endif
