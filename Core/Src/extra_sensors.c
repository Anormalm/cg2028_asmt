#include "main.h"
#include "extra_sensors.h"
#include "sensor_rules.h"
#include "buzzer.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_FRAME          256    // samples per half of the DMA buffer
#define DBFS_FLOOR          -960    // -96.0 dBFS, basically silence
#define RMS_FLOOR       0.00001585f // 10^(-96/20), anything below reads as the floor
/* Sinc3 filter with 128x oversampling gives a full scale of 128^3 = 2^21. */
#define PCM_FULL_SCALE  2097152.0f

#define BUZZER_MASK_MS       400U   // ignore the mic while (and just after) our buzzer is on
#define AUDIO_STALE_MS       200U   // no new frame for this long -> reading is invalid
#define LOUD_REPEAT_MS      2000U   // min gap between counted loud events
#define QUIET_HOLD_MS        500U   // must be quiet this long before "loud" clears
#define LOUD_HYSTERESIS       30    // 3 dB below the threshold to count as quiet
#define UPLOAD_WINDOW_MS     500U   // peaks older than this are thrown away

// DFSDM / DMA error codes reported in mic_error
enum {
    MIC_OK = 0,
    MIC_ERR_CLOCK,
    MIC_ERR_CHANNEL,
    MIC_ERR_FILTER,
    MIC_ERR_DMA,
    MIC_ERR_REG_CHANNEL,
    MIC_ERR_START,
    MIC_ERR_RUNTIME
};

static DFSDM_Channel_HandleTypeDef channel;
static DFSDM_Filter_HandleTypeDef filter;
static DMA_HandleTypeDef microphone_dma;

static int32_t audio_dma[2 * AUDIO_FRAME];  // circular DMA buffer (two halves)
static int32_t audio_latest[AUDIO_FRAME];   // last finished half, copied in the ISR
static int32_t audio_work[AUDIO_FRAME];     // our own copy to do the maths on
static volatile uint32_t audio_sequence;    // bumps every time a new frame lands
static volatile uint32_t audio_fault;

static uint32_t consumed;                   // last audio_sequence we processed
static uint32_t audio_at, buzzer_at, loud_at, quiet_at;
static int buzzer_seen, loud, loud_seen;

/* Config order is {revision, sound enabled, threshold}.
 * Default: sound on, -30.0 dBFS threshold. The server can change it later. */
static SensorConfig requested = {0, 1, -300};
static SensorConfig config    = {0, 1, -300};

static SensorSnapshot current = {.sound_dbfs = DBFS_FLOOR};
static SensorSnapshot published;            // what the network task gets to see

// loudest reading since the last upload, so short bursts between posts aren't missed
static int upload_peak = DBFS_FLOOR;
static int upload_active;
static int upload_valid;
static uint32_t upload_at;

// MICROPHONE SETUP

// returns 0 on success, otherwise which step failed
static int Microphone_Init(void)
{
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();

    RCC_PeriphCLKInitTypeDef clock = {0};
    clock.PeriphClockSelection = RCC_PERIPHCLK_DFSDM1;
    clock.Dfsdm1ClockSelection = RCC_DFSDM1CLKSOURCE_SYSCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return MIC_ERR_CLOCK;

    // PE9 = mic clock out, PE7 = mic data in
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_7 | GPIO_PIN_9;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = GPIO_AF6_DFSDM1;
    HAL_GPIO_Init(GPIOE, &pins);

    channel.Instance = DFSDM1_Channel2;
    channel.Init.OutputClock.Activation = ENABLE;
    channel.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_SYSTEM;
    channel.Init.OutputClock.Divider = 40;          // 80 MHz / 40 = 2 MHz PDM clock
    channel.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    channel.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    channel.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
    channel.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    channel.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    channel.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    channel.Init.Awd.Oversampling = 10;
    channel.Init.Offset = 0;
    channel.Init.RightBitShift = 0;
    if (HAL_DFSDM_ChannelInit(&channel) != HAL_OK) return MIC_ERR_CHANNEL;

    filter.Instance = DFSDM1_Filter0;
    filter.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    filter.Init.RegularParam.FastMode = ENABLE;
    filter.Init.RegularParam.DmaMode = ENABLE;
    filter.Init.InjectedParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
    filter.Init.InjectedParam.ScanMode = DISABLE;
    filter.Init.InjectedParam.DmaMode = DISABLE;
    filter.Init.InjectedParam.ExtTrigger = DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
    filter.Init.InjectedParam.ExtTriggerEdge = DFSDM_FILTER_EXT_TRIG_RISING_EDGE;
    filter.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC3_ORDER;
    filter.Init.FilterParam.Oversampling = 128;     // 2 MHz / 128 = 15625 samples/s
    filter.Init.FilterParam.IntOversampling = 1;
    if (HAL_DFSDM_FilterInit(&filter) != HAL_OK) return MIC_ERR_FILTER;

    // circular DMA so the buffer keeps refilling without us restarting it
    microphone_dma.Instance = DMA1_Channel4;
    microphone_dma.Init.Request = DMA_REQUEST_DFSDM1_FLT0;
    microphone_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    microphone_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    microphone_dma.Init.MemInc = DMA_MINC_ENABLE;
    microphone_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    microphone_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    microphone_dma.Init.Mode = DMA_CIRCULAR;
    microphone_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&microphone_dma) != HAL_OK) return MIC_ERR_DMA;
    __HAL_LINKDMA(&filter, hdmaReg, microphone_dma);

    if (HAL_DFSDM_FilterConfigRegChannel(&filter, DFSDM_CHANNEL_2, DFSDM_CONTINUOUS_CONV_ON) != HAL_OK)
        return MIC_ERR_REG_CHANNEL;

    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

    if (HAL_DFSDM_FilterRegularStart_DMA(&filter, audio_dma, 2 * AUDIO_FRAME) != HAL_OK)
        return MIC_ERR_START;
    return MIC_OK;
}

//interrupts

void DMA1_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&microphone_dma);
}

/* Copy the half that just finished before the DMA comes back around to it.
 * Kept short on purpose - no float maths or RTOS calls inside the ISR. */
static void AudioFrame(const int32_t *samples)
{
    memcpy(audio_latest, samples, sizeof(audio_latest));
    ++audio_sequence;
}

void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{
    if (h == &filter) AudioFrame(audio_dma);                // first half ready
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{
    if (h == &filter) AudioFrame(audio_dma + AUDIO_FRAME);  // second half ready
}

void HAL_DFSDM_FilterErrorCallback(DFSDM_Filter_HandleTypeDef *h)
{
    if (h == &filter) audio_fault = MIC_ERR_RUNTIME;
}

void ExtraSensors_Init(void)
{
    current.mic_error = Microphone_Init();
    published = current;
}

// network task calls this when the server sends new settings; applied on the next Update
void ExtraSensors_Configure(const SensorConfig *c)
{
    if (!SensorConfig_Valid(c)) return;
    taskENTER_CRITICAL();
    requested = *c;
    taskEXIT_CRITICAL();
}

/* Network task grabs the latest reading here. If there was a loud burst since
 * the last upload we report that peak instead, then reset it so old
 * activity isn't sent twice. */
void ExtraSensors_Get(SensorSnapshot *s)
{
    taskENTER_CRITICAL();
    *s = published;
    if (upload_valid && s->sound_valid && !s->sound_masked) {
        s->sound_dbfs = upload_peak;
        s->sound_active = upload_active;
    }
    upload_peak = DBFS_FLOOR;
    upload_active = 0;
    upload_valid = 0;
    taskEXIT_CRITICAL();
}

// RMS of one frame -> dBFS (in tenths), clamped to [-96.0, 0]
static int FrameToDbfs(const int32_t *frame)
{
    float mean = 0.0f;
    float squares = 0.0f;

    // DFSDM puts the 24 bit result in the top bits, >> 8 to get the real value
    for (int i = 0; i < AUDIO_FRAME; ++i)
        mean += (float)(frame[i] >> 8);
    mean /= (float)AUDIO_FRAME;

    // take the mean out first so any DC offset from the mic doesn't count as sound
    for (int i = 0; i < AUDIO_FRAME; ++i) {
        float a = (float)(frame[i] >> 8) - mean;
        squares += a * a;
    }
    float rms = sqrtf(squares / (float)AUDIO_FRAME) / PCM_FULL_SCALE;

    // 200 * log10 = 10 * (20 log10), i.e. dB in tenths
    int dbfs = (rms > RMS_FLOOR) ? (int)(200.0f * log10f(rms)) : DBFS_FLOOR;
    if (dbfs > 0) dbfs = 0;
    if (dbfs < DBFS_FLOOR) dbfs = DBFS_FLOOR;
    return dbfs;
}

/* Called from the sensor task every 20 ms. */
void ExtraSensors_Update(uint32_t now)
{
    taskENTER_CRITICAL();
    config = requested;
    taskEXIT_CRITICAL();

    current.uptime_ms = now;
    current.config_revision = config.revision;

    // our own buzzer would set off the mic, so mask it while it's on (plus a bit after)
    if (buzzer_frequency_hz) {
        buzzer_at = now;
        buzzer_seen = 1;
    }
    current.sound_masked = buzzer_seen && (uint32_t)(now - buzzer_at) < BUZZER_MASK_MS;

    // grab the newest frame (if there is one) without the ISR changing it under us
    uint32_t seq;
    taskENTER_CRITICAL();
    seq = audio_sequence;
    if (seq != consumed) memcpy(audio_work, audio_latest, sizeof(audio_work));
    taskEXIT_CRITICAL();

    if (seq != consumed) {
        current.audio_overruns += seq - consumed - 1U;  // frames we skipped over
        consumed = seq;
        audio_at = now;
        current.sound_dbfs = FrameToDbfs(audio_work);
    }

    if (audio_fault) current.mic_error = (int)audio_fault;
    current.sound_valid = config.sound && !current.mic_error && seq &&
                          (uint32_t)(now - audio_at) < AUDIO_STALE_MS;

    /* Loud detection with hysteresis: goes loud above the threshold, only
     * clears once it's 3 dB under for 0.5 s. New events are counted at most
     * once every 2 s so one long noise doesn't spam the count. */
    int usable = current.sound_valid && !current.sound_masked;
    if (usable && current.sound_dbfs >= config.sound_threshold) {
        if (!loud && (!loud_seen || (uint32_t)(now - loud_at) >= LOUD_REPEAT_MS)) {
            ++current.sound_events;
            loud_at = now;
            loud_seen = 1;
        }
        loud = 1;
        quiet_at = now;
    } else if (!usable ||
               (current.sound_dbfs < config.sound_threshold - LOUD_HYSTERESIS &&
                (uint32_t)(now - quiet_at) >= QUIET_HOLD_MS)) {
        loud = 0;
    }
    current.sound_active = loud && usable;

    taskENTER_CRITICAL();
    published = current;

    // if the network's been down a while, drop the old peak instead of sending stale activity
    if (!upload_valid || (uint32_t)(now - upload_at) >= UPLOAD_WINDOW_MS) {
        upload_peak = DBFS_FLOOR;
        upload_active = 0;
        upload_at = now;
    }
    if (usable) {
        if (current.sound_dbfs > upload_peak) upload_peak = current.sound_dbfs;
        upload_active |= current.sound_active;
        upload_valid = 1;
    } else {
        upload_peak = DBFS_FLOOR;
        upload_active = 0;
        upload_valid = 0;
    }
    taskEXIT_CRITICAL();
}

/* Server reply to /api/sensors looks like:
 *   HTTP/1.1 200 ...\r\n\r\nACK <boot>-s<seq>\nSOUND <revision> <on> <threshold>\n
 * Returns 1 only once the whole thing has arrived and is valid. */
int ExtraSensors_ParseReply(const char *response, const char *expected)
{
    const char *body = strstr(response, "\r\n\r\n");
    if (!body) return 0;
    if (strncmp(response, "HTTP/1.0 200 ", 13) && strncmp(response, "HTTP/1.1 200 ", 13)) return 0;
    body += 4;

    size_t n = strlen(expected);
    if (strncmp(body, expected, n)) return 0;

    SensorConfig c = {0};
    unsigned long revision;
    int used = 0;
    const char *line = body + n;
    if (sscanf(line, "SOUND %lu %u %d%n", &revision, &c.sound, &c.sound_threshold, &used) != 3)
        return 0;
    if (strcmp(line + used, "\n")) return 0;    // line not finished yet, wait for more TCP data

    c.revision = (uint32_t)revision;
    if (!SensorConfig_Valid(&c)) return 0;
    ExtraSensors_Configure(&c);
    return 1;
}
