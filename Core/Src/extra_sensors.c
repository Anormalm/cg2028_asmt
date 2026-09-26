#include "main.h"
#include "extra_sensors.h"
#include "sensor_rules.h"
#include "buzzer.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../ToF/vl53l0x_api.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hI2cHandler;
static VL53L0X_Dev_t tof;
static int tof_ready;
static DFSDM_Channel_HandleTypeDef channel;
static DFSDM_Filter_HandleTypeDef filter;
static DMA_HandleTypeDef microphone_dma;
static int32_t audio_dma[512], audio_latest[256], audio_work[256];
static volatile uint32_t audio_sequence, audio_fault;
static uint32_t consumed, audio_at, range_at, poll_at, buzzer_at, loud_at, quiet_at, prox_at;
static int buzzer_seen, loud, loud_seen, prox_votes;
static SensorConfig requested = {0,1,1,0,300,800,-300};
static SensorConfig config = {0,1,1,0,300,800,-300};
static SensorSnapshot current = {.distance_mm=-1, .range_status=-2, .sound_dbfs=-960};
static SensorSnapshot published;

static int Microphone_Init(void)
{
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    RCC_PeriphCLKInitTypeDef clock = {0};
    clock.PeriphClockSelection = RCC_PERIPHCLK_DFSDM1;
    clock.Dfsdm1ClockSelection = RCC_DFSDM1CLKSOURCE_SYSCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return 1;
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
    channel.Init.OutputClock.Divider = 40; /* 80 MHz / 40 = 2 MHz PDM clock. */
    channel.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
    channel.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
    channel.Init.Input.Pins = DFSDM_CHANNEL_SAME_CHANNEL_PINS;
    channel.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_RISING;
    channel.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
    channel.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
    channel.Init.Awd.Oversampling = 10;
    channel.Init.Offset = 0;
    channel.Init.RightBitShift = 0;
    if (HAL_DFSDM_ChannelInit(&channel) != HAL_OK) return 2;
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
    filter.Init.FilterParam.Oversampling = 128; /* 15625 PCM samples/s. */
    filter.Init.FilterParam.IntOversampling = 1;
    if (HAL_DFSDM_FilterInit(&filter) != HAL_OK) return 3;
    microphone_dma.Instance = DMA1_Channel4;
    microphone_dma.Init.Request = DMA_REQUEST_DFSDM1_FLT0;
    microphone_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    microphone_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    microphone_dma.Init.MemInc = DMA_MINC_ENABLE;
    microphone_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    microphone_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    microphone_dma.Init.Mode = DMA_CIRCULAR;
    microphone_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&microphone_dma) != HAL_OK) return 4;
    __HAL_LINKDMA(&filter, hdmaReg, microphone_dma);
    if (HAL_DFSDM_FilterConfigRegChannel(&filter, DFSDM_CHANNEL_2, DFSDM_CONTINUOUS_CONV_ON) != HAL_OK) return 5;
    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    return HAL_DFSDM_FilterRegularStart_DMA(&filter, audio_dma, 512) == HAL_OK ? 0 : 6;
}
void DMA1_Channel4_IRQHandler(void) { HAL_DMA_IRQHandler(&microphone_dma); }
static void AudioFrame(const int32_t *samples)
{
    /* Copy the completed half before DMA reuses it. No float/RTOS work in ISR. */
    memcpy(audio_latest, samples, sizeof(audio_latest));
    ++audio_sequence;
}
void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{ if (h == &filter) AudioFrame(audio_dma); }
void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *h)
{ if (h == &filter) AudioFrame(audio_dma+256); }
void HAL_DFSDM_FilterErrorCallback(DFSDM_Filter_HandleTypeDef *h)
{ if (h == &filter) audio_fault = 7; }

static int Proximity_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef pin = {0};
    pin.Pin = VL53L0X_XSHUT_Pin;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(VL53L0X_XSHUT_GPIO_Port, &pin);
    HAL_GPIO_WritePin(VL53L0X_XSHUT_GPIO_Port, pin.Pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(VL53L0X_XSHUT_GPIO_Port, pin.Pin, GPIO_PIN_SET);
    HAL_Delay(5);
    tof.I2cHandle = &hI2cHandler;
    tof.I2cDevAddr = 0x52;
    uint8_t vhv, phase, aperture;
    uint32_t spads;
    if (VL53L0X_DataInit(&tof) || VL53L0X_StaticInit(&tof) ||
        VL53L0X_PerformRefCalibration(&tof, &vhv, &phase) ||
        VL53L0X_PerformRefSpadManagement(&tof, &spads, &aperture) ||
        VL53L0X_SetDeviceMode(&tof, VL53L0X_DEVICEMODE_CONTINUOUS_RANGING) ||
        VL53L0X_SetMeasurementTimingBudgetMicroSeconds(&tof, 33000) ||
        VL53L0X_StartMeasurement(&tof)) return 0;
    return 1;
}
void ExtraSensors_Init(void)
{
    /* Startup only: calibration waits occur before the scheduler starts. */
    tof_ready = Proximity_Init();
    current.mic_error = Microphone_Init();
    published = current;
}
void ExtraSensors_Configure(const SensorConfig *c)
{
    if (!SensorConfig_Valid(c)) return;
    taskENTER_CRITICAL(); requested = *c; taskEXIT_CRITICAL();
}
void ExtraSensors_Get(SensorSnapshot *s)
{ taskENTER_CRITICAL(); *s = published; taskEXIT_CRITICAL(); }

void ExtraSensors_Update(uint32_t now)
{
    taskENTER_CRITICAL(); config = requested; taskEXIT_CRITICAL();
    current.uptime_ms = now;
    current.config_revision = config.revision;
    if (buzzer_frequency_hz) { buzzer_at=now; buzzer_seen=1; }
    current.sound_masked = buzzer_seen && (uint32_t)(now-buzzer_at)<400U;
    if (tof_ready && (uint32_t)(now-poll_at)>=100U) {
        poll_at=now;
        int new_range=0;
        uint8_t ready=0;
        VL53L0X_RangingMeasurementData_t result;
        if (VL53L0X_GetMeasurementDataReady(&tof,&ready)) {
            current.range_status=-2; current.distance_mm=-1;
        } else if (ready) {
            if (VL53L0X_GetRangingMeasurementData(&tof,&result) || VL53L0X_ClearInterruptMask(&tof,0)) {
                current.range_status=-2; current.distance_mm=-1;
            } else {
                range_at=now;
                new_range=1;
                current.range_status=result.RangeStatus;
                current.distance_mm=result.RangeStatus==0 && result.RangeMilliMeter<=2000 ? result.RangeMilliMeter : -1;
            }
        }
        if (new_range && current.distance_mm>=0 && current.distance_mm<(int)config.far_mm) {
            if (prox_votes<2) ++prox_votes;
            if(prox_votes==2 && !current.proximity_active) {current.proximity_active=1;prox_at=now;}
        } else if(current.distance_mm<0 || current.distance_mm>(int)config.far_mm+50) {
            prox_votes=0; current.proximity_active=0;
        }
    }
    if ((uint32_t)(now-range_at)>300U || !config.proximity) {
        current.distance_mm=-1; current.proximity_active=0; prox_votes=0;
        if (!config.proximity) current.range_status=-1;
    }
    uint32_t seq;
    taskENTER_CRITICAL();
    seq=audio_sequence;
    if(seq!=consumed) memcpy(audio_work,audio_latest,sizeof(audio_work));
    taskEXIT_CRITICAL();
    if (seq!=consumed) {
        current.audio_overruns += seq-consumed-1U;
        consumed=seq; audio_at=now;
        float mean=0, squares=0;
        for(int i=0;i<256;++i) mean+=(float)(audio_work[i]>>8);
        mean/=256.0f;
        for(int i=0;i<256;++i) {float a=(float)(audio_work[i]>>8)-mean;squares+=a*a;}
        float rms=sqrtf(squares/256.0f)/2097152.0f;
        current.sound_dbfs = rms>0.00001585f ? (int)(200.0f*log10f(rms)) : -960;
        if(current.sound_dbfs>0) current.sound_dbfs=0;
        if(current.sound_dbfs<-960) current.sound_dbfs=-960;
    }
    if(audio_fault) current.mic_error=(int)audio_fault;
    current.sound_valid=config.sound && !current.mic_error && seq && (uint32_t)(now-audio_at)<200U;
    if(current.sound_valid && !current.sound_masked && current.sound_dbfs>=config.sound_threshold) {
        if(!loud && (!loud_seen || (uint32_t)(now-loud_at)>=2000U)) {
            ++current.sound_events; loud_at=now; loud_seen=1;
        }
        loud=1;quiet_at=now;
    } else if(!current.sound_valid || current.sound_masked ||
              (current.sound_dbfs<config.sound_threshold-30 && (uint32_t)(now-quiet_at)>=500U)) loud=0;
    current.sound_active=loud && current.sound_valid && !current.sound_masked;
    taskENTER_CRITICAL(); published=current; taskEXIT_CRITICAL();
}
uint32_t ExtraSensors_ProximityTone(uint32_t now)
{ return ProximityTone(current.proximity_active,current.distance_mm,now-prox_at,&config); }

int ExtraSensors_ParseReply(const char *response, const char *expected)
{
    const char *body=strstr(response,"\r\n\r\n");
    if(!body || (strncmp(response,"HTTP/1.0 200 ",13) && strncmp(response,"HTTP/1.1 200 ",13))) return 0;
    body+=4;
    size_t n=strlen(expected);
    if(strncmp(body,expected,n)) return 0;
    SensorConfig c={0}; unsigned long revision; int used=0;
    const char *line=body+n;
    if(sscanf(line,"CFG %lu %u %u %u %u %u %d%n",&revision,&c.proximity,&c.sound,&c.beeps,&c.near_mm,&c.far_mm,&c.sound_threshold,&used)!=7) return 0;
    if(strcmp(line+used,"\n")) return 0; /* Wait for a complete TCP response. */
    c.revision=(uint32_t)revision;
    if(!SensorConfig_Valid(&c)) return 0;
    ExtraSensors_Configure(&c);
    return 1;
}
