#ifndef MOTION_CAPTURE_H
#define MOTION_CAPTURE_H
#include <stdint.h>
#define CAPTURE_PRE 100U
#define CAPTURE_POST 200U
#define CAPTURE_ROWS (CAPTURE_PRE + CAPTURE_POST)
#define CAPTURE_CHUNK 3U
typedef struct {
    uint32_t time_ms;
    int32_t ar[3], af[3], gr[3], gf[3];
    uint32_t state, flags;
} MotionSample;
typedef struct {
    uint32_t id, trigger_ms, count, pre_count;
    char source[16], outcome[24];
} CaptureInfo;
/* Set to 1 from the debugger to request a capture on the next sample. */
extern volatile uint32_t debug_capture_request;
extern volatile uint32_t capture_dropped, capture_completed, capture_uploaded;
void MotionCapture_Add(const MotionSample *sample, const char *trigger, const char *outcome);
int MotionCapture_Read(uint32_t part, CaptureInfo *info, MotionSample samples[CAPTURE_CHUNK], uint32_t *rows);
void MotionCapture_Release(uint32_t id);
#endif
