#include "motion_capture.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define CAPTURE_SLOTS 2

enum { SLOT_FREE = 0, SLOT_RECORDING = 1, SLOT_READY = 2 };

typedef struct {
    CaptureInfo info;
    MotionSample samples[CAPTURE_ROWS];
    int state;                          // SLOT_FREE / SLOT_RECORDING / SLOT_READY
} CaptureSlot;

static CaptureSlot slots[CAPTURE_SLOTS];
static MotionSample history[CAPTURE_PRE];   // ring buffer of the most recent samples
static uint32_t head;                       // next write position in history
static uint32_t history_count;
static uint32_t next_id;
static int active = -1;                     // slot being recorded, -1 = none

// globals so we can poke/watch them from the debugger
volatile uint32_t debug_capture_request;    // set to 1 to force a capture
volatile uint32_t capture_dropped;          // trigger came but both slots were full
volatile uint32_t capture_completed;
volatile uint32_t capture_uploaded;

// grab a free slot and fill it with the pre-trigger history
static void StartCapture(const MotionSample *sample, const char *trigger)
{
    taskENTER_CRITICAL();
    for (int i = 0; i < CAPTURE_SLOTS; ++i) {
        if (slots[i].state == SLOT_FREE) {
            active = i;
            slots[i].state = SLOT_RECORDING;
            break;
        }
    }
    taskEXIT_CRITICAL();

    if (active < 0) {       // nothing free, skip this one
        ++capture_dropped;
        return;
    }

    CaptureSlot *slot = &slots[active];
    slot->info = (CaptureInfo){0};
    slot->info.id = ++next_id;
    slot->info.trigger_ms = sample->time_ms;
    slot->info.count = slot->info.pre_count = history_count;
    strncpy(slot->info.source, trigger, sizeof(slot->info.source) - 1);
    strcpy(slot->info.outcome, "no_candidate");     // overwritten if the detector decides something

    // copy history oldest -> newest
    for (uint32_t i = 0; i < history_count; ++i)
        slot->samples[i] = history[(head + CAPTURE_PRE - history_count + i) % CAPTURE_PRE];
}

/* Called once per sample. trigger != NULL starts a new capture (if we aren't
 * already recording), outcome != NULL tags the current capture with what
 * the detector ended up deciding. */
void MotionCapture_Add(const MotionSample *sample, const char *trigger, const char *outcome)
{
    if (trigger && active < 0)
        StartCapture(sample, trigger);

    if (active >= 0) {
        CaptureSlot *slot = &slots[active];
        slot->samples[slot->info.count++] = *sample;

        if (outcome) {
            strncpy(slot->info.outcome, outcome, sizeof(slot->info.outcome) - 1);
            slot->info.outcome[sizeof(slot->info.outcome) - 1] = '\0';
        }

        // got enough after-trigger samples -> hand it over to the network task
        if (slot->info.count >= slot->info.pre_count + CAPTURE_POST) {
            taskENTER_CRITICAL();
            slot->state = SLOT_READY;
            active = -1;
            ++capture_completed;
            taskEXIT_CRITICAL();
        }
    }

    // always keep the history going for the next capture
    history[head] = *sample;
    head = (head + 1) % CAPTURE_PRE;
    if (history_count < CAPTURE_PRE) ++history_count;
}

/* Network task: copy out chunk `part` of the oldest ready capture.
 * Returns 0 if nothing is ready (or part is past the end). */
int MotionCapture_Read(uint32_t part, CaptureInfo *info,
                       MotionSample samples[CAPTURE_CHUNK], uint32_t *rows)
{
    taskENTER_CRITICAL();

    // oldest first so they upload in order
    int chosen = -1;
    for (int i = 0; i < CAPTURE_SLOTS; ++i) {
        if (slots[i].state == SLOT_READY &&
            (chosen < 0 || slots[i].info.id < slots[chosen].info.id)) {
            chosen = i;
        }
    }
    if (chosen < 0 || part * CAPTURE_CHUNK >= slots[chosen].info.count) {
        taskEXIT_CRITICAL();
        return 0;
    }

    const CaptureSlot *slot = &slots[chosen];
    *info = slot->info;
    *rows = info->count - part * CAPTURE_CHUNK;
    if (*rows > CAPTURE_CHUNK) *rows = CAPTURE_CHUNK;   // last chunk can be shorter
    memcpy(samples, &slot->samples[part * CAPTURE_CHUNK], *rows * sizeof(MotionSample));

    taskEXIT_CRITICAL();
    return 1;
}

// whole capture uploaded -> free the slot
void MotionCapture_Release(uint32_t id)
{
    taskENTER_CRITICAL();
    for (int i = 0; i < CAPTURE_SLOTS; ++i) {
        if (slots[i].state == SLOT_READY && slots[i].info.id == id) {
            slots[i].state = SLOT_FREE;
            ++capture_uploaded;
            break;
        }
    }
    taskEXIT_CRITICAL();
}
