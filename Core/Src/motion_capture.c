#include "motion_capture.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
/* Sensor task is the only writer. Network reads only completed, immutable slots.
 * Two completed captures survive a temporary outage, until upload or board reset.
 */
typedef struct { CaptureInfo info; MotionSample samples[CAPTURE_ROWS]; int state; } CaptureSlot;
static CaptureSlot slots[2]; /* 0=free, 1=recording, 2=ready */
static MotionSample history[CAPTURE_PRE];
static uint32_t head, history_count, next_id;
static int active = -1;
volatile uint32_t debug_capture_request, capture_dropped, capture_completed, capture_uploaded;

void MotionCapture_Add(const MotionSample *sample, const char *trigger, const char *outcome)
{
    if (trigger && active < 0) {
        taskENTER_CRITICAL();
        for (int i = 0; i < 2; ++i)
            if (!slots[i].state) { active = i; slots[i].state = 1; break; }
        taskEXIT_CRITICAL();
        if (active < 0) ++capture_dropped;
        else {
            CaptureSlot *slot = &slots[active];
            slot->info = (CaptureInfo){0};
            slot->info.id = ++next_id;
            slot->info.trigger_ms = sample->time_ms;
            slot->info.count = slot->info.pre_count = history_count;
            strncpy(slot->info.source, trigger, sizeof(slot->info.source)-1);
            strcpy(slot->info.outcome, "no_candidate");
            for (uint32_t i = 0; i < history_count; ++i)
                slot->samples[i] = history[(head + CAPTURE_PRE - history_count + i) % CAPTURE_PRE];
        }
    }
    if (active >= 0) {
        CaptureSlot *slot = &slots[active];
        slot->samples[slot->info.count++] = *sample;
        if (outcome) {
            strncpy(slot->info.outcome, outcome, sizeof(slot->info.outcome)-1);
            slot->info.outcome[sizeof(slot->info.outcome)-1] = 0;
        }
        if (slot->info.count >= slot->info.pre_count + CAPTURE_POST) {
            taskENTER_CRITICAL();
            slot->state = 2;
            active = -1;
            ++capture_completed;
            taskEXIT_CRITICAL();
        }
    }
    history[head] = *sample;
    head = (head + 1) % CAPTURE_PRE;
    if (history_count < CAPTURE_PRE) ++history_count;
}

int MotionCapture_Read(uint32_t part, CaptureInfo *info, MotionSample samples[CAPTURE_CHUNK], uint32_t *rows)
{
    taskENTER_CRITICAL();
    int chosen = -1;
    for (int i = 0; i < 2; ++i)
        if (slots[i].state == 2 && (chosen < 0 || slots[i].info.id < slots[chosen].info.id)) chosen = i;
    if (chosen < 0 || part * CAPTURE_CHUNK >= slots[chosen].info.count) {
        taskEXIT_CRITICAL(); return 0;
    }
    CaptureSlot *slot = &slots[chosen];
    *info = slot->info;
    *rows = info->count - part * CAPTURE_CHUNK;
    if (*rows > CAPTURE_CHUNK) *rows = CAPTURE_CHUNK;
    memcpy(samples, &slot->samples[part * CAPTURE_CHUNK], *rows * sizeof(MotionSample));
    taskEXIT_CRITICAL();
    return 1;
}

void MotionCapture_Release(uint32_t id)
{
    taskENTER_CRITICAL();
    for (int i = 0; i < 2; ++i)
        if (slots[i].state == 2 && slots[i].info.id == id) {
            slots[i].state = 0; ++capture_uploaded; break;
        }
    taskEXIT_CRITICAL();
}
