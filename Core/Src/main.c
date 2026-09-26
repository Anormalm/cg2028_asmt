/******************************************************************************
 * @file           : main.c
 * @brief          : CG2028 Assignment - ElderCare Wearable Safety Companion
 * @author         : Hou Linxin
 * (c) CG2028 Teaching Team
 ******************************************************************************/

/*--------------------------- Includes ---------------------------------------*/
#include "main.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_accelero.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_gyro.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

/*--------------------------- Configuration ----------------------------------*/
#define EWMA_ALPHA_ACCEL_PERCENT   50
#define EWMA_ALPHA_GYRO_PERCENT    50

#define SAMPLE_INTERVAL_MS          20
#define NORMAL_LED_DELAY_MS       1000
#define FALL_LED_DELAY_MS          150
#define UART_PRINT_INTERVAL_MS   200

static void UART1_Init(void);
static void UART_Send(const char *text);

extern int ewma_filter(int new_data, int old_output, int alpha_percent);
int ewma_filter_C(int new_data, int old_output, int alpha_percent);

UART_HandleTypeDef huart1;

/* Experimental starting thresholds, applied to ASSEMBLY-filtered magnitudes.
 * BSP units: acceleration mg, gyro mdps. At 50 Hz, alpha=50 retains short
 * events better than alpha=25. Validate thresholds with recorded board trials.
 * The BSP's +/-2 g accelerometer range can clip strong impacts.
 */
#define LOW_ACCEL_MPS2       (0.65f * 9.80665f)
#define IMPACT_ACCEL_MPS2    (1.60f * 9.80665f)
#define ROTATION_DPS         100.0f
#define QUIET_GYRO_DPS        20.0f
#define QUIET_ACCEL_MIN      (0.85f * 9.80665f)
#define QUIET_ACCEL_MAX      (1.15f * 9.80665f)
#define FILTER_SETTLE_MS     1000U
#define IMPACT_WINDOW_MS      700U
#define CONFIRM_WINDOW_MS    2500U
#define QUIET_REQUIRED_MS    1000U
#define ACK_HOLD_MS          1000U

typedef enum {
    FD_STARTUP, FD_NORMAL, FD_WAIT_IMPACT, FD_CONFIRM, FD_FALL_LATCHED
} FallState;

static FallState fall_state = FD_STARTUP;
static uint32_t state_since, quiet_since, button_since;
static int rotation_seen, quiet_tracking, button_tracking, ack_armed;

static const char *FallState_Name(void)
{
    switch (fall_state) {
        case FD_STARTUP: return "STARTUP";
        case FD_NORMAL: return "NORMAL";
        case FD_WAIT_IMPACT: return "WAIT_IMPACT";
        case FD_CONFIRM: return "CONFIRM";
        case FD_FALL_LATCHED: return "FALL";
        default: return "UNKNOWN";
    }
}

static void FallDetector_Update(float accel, float gyro, uint32_t now)
{
    switch (fall_state) {
        case FD_STARTUP:
            /* Do not interpret zero-initialized filter startup as free fall. */
            if ((uint32_t)(now - state_since) >= FILTER_SETTLE_MS)
                fall_state = FD_NORMAL;
            break;
        case FD_NORMAL:
            if (accel < LOW_ACCEL_MPS2) {
                rotation_seen = (gyro >= ROTATION_DPS);
                quiet_tracking = 0;
                state_since = now;
                fall_state = FD_WAIT_IMPACT;
            }
            break;
        case FD_WAIT_IMPACT:
            if (gyro >= ROTATION_DPS) rotation_seen = 1;
            if ((uint32_t)(now - state_since) > IMPACT_WINDOW_MS) {
                fall_state = FD_NORMAL;
            } else if (accel >= IMPACT_ACCEL_MPS2) {
                quiet_tracking = 0;
                state_since = now;
                fall_state = FD_CONFIRM;
            }
            break;
        case FD_CONFIRM:
            /* Allow a short gyro/filter lag around impact. */
            if ((uint32_t)(now - state_since) <= 300U && gyro >= ROTATION_DPS)
                rotation_seen = 1;
            if ((uint32_t)(now - state_since) > CONFIRM_WINDOW_MS) {
                quiet_tracking = 0;
                fall_state = FD_NORMAL;
                break;
            }
            if (accel >= QUIET_ACCEL_MIN && accel <= QUIET_ACCEL_MAX &&
                gyro < QUIET_GYRO_DPS) {
                if (!quiet_tracking) {
                    quiet_since = now;
                    quiet_tracking = 1;
                }
                if (rotation_seen &&
                    (uint32_t)(now - quiet_since) >= QUIET_REQUIRED_MS) {
                    fall_state = FD_FALL_LATCHED;
                    state_since = now;
                    button_tracking = 0;
                    ack_armed = 0;
                }
            } else {
                quiet_tracking = 0;
            }
            break;
        case FD_FALL_LATCHED:
            /* B2 is active LOW. Require release after detection, then a hold. */
            if (BSP_PB_GetState(BUTTON_USER) != GPIO_PIN_RESET) {
                ack_armed = 1;
                button_tracking = 0;
            } else if (ack_armed) {
                if (!button_tracking) {
                    button_since = now;
                    button_tracking = 1;
                } else if ((uint32_t)(now - button_since) >= ACK_HOLD_MS) {
                    fall_state = FD_STARTUP;
                    state_since = now;
                    rotation_seen = quiet_tracking = button_tracking = ack_armed = 0;
                }
            }
            break;
    }
}

int main(void)
{
    HAL_Init();
    UART1_Init();
    BSP_LED_Init(LED2);
    BSP_LED_Off(LED2);
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_GPIO);
    if (BSP_ACCELERO_Init() != ACCELERO_OK || BSP_GYRO_Init() != GYRO_OK) {
        UART_Send("ERROR: motion sensor initialization failed\r\n");
        BSP_LED_On(LED2);
        while (1) { HAL_Delay(100); }
    }
    UART_Send("ElderCare: 50Hz target; Ar/Af=mg, Gr/Gf=mdps; hold B2 1s to acknowledge FALL\r\n");

    /* Previous EWMA outputs. The first test/application sample starts from 0. */
    int accel_ewma_asm[3] = {0, 0, 0};
    int gyro_ewma_asm[3]  = {0, 0, 0};

    /* Reference C states are kept separately for assembly verification. */
    int accel_ewma_c[3] = {0, 0, 0};
    int gyro_ewma_c[3]  = {0, 0, 0};

    unsigned long sample_number = 0;

    uint32_t last_led_toggle = HAL_GetTick();
    uint32_t last_uart_print = HAL_GetTick();
    uint32_t last_sample = HAL_GetTick();
    uint32_t mismatch_samples = 0;
    uint32_t max_sample_dt = 0;
    state_since = HAL_GetTick();

    while (1)
    {
        uint32_t sample_tick = HAL_GetTick();
        uint32_t sample_dt = (uint32_t)(sample_tick - last_sample);
        if (sample_dt < SAMPLE_INTERVAL_MS) {
            HAL_Delay(1);
            continue;
        }
        last_sample = sample_tick;
        if (sample_dt > max_sample_dt) max_sample_dt = sample_dt;
        int16_t accel_raw_i16[3] = {0, 0, 0};
        float gyro_raw_float[3] = {0.0f, 0.0f, 0.0f};
        int gyro_raw_int[3] = {0, 0, 0};

        BSP_ACCELERO_AccGetXYZ(accel_raw_i16);
        BSP_GYRO_GetXYZ(gyro_raw_float);

        /* The supplied BSP reports gyroscope readings as floating-point raw
         * values. Convert them to signed integers before passing them to the
         * integer assembly routine. */
        for (int axis = 0; axis < 3; axis++)
        {
            gyro_raw_int[axis] = (int)gyro_raw_float[axis];

            accel_ewma_asm[axis] = ewma_filter(
                (int)accel_raw_i16[axis],
                accel_ewma_asm[axis],
                EWMA_ALPHA_ACCEL_PERCENT);

            gyro_ewma_asm[axis] = ewma_filter(
                gyro_raw_int[axis],
                gyro_ewma_asm[axis],
                EWMA_ALPHA_GYRO_PERCENT);

            accel_ewma_c[axis] = ewma_filter_C(
                (int)accel_raw_i16[axis],
                accel_ewma_c[axis],
                EWMA_ALPHA_ACCEL_PERCENT);

            gyro_ewma_c[axis] = ewma_filter_C(
                gyro_raw_int[axis],
                gyro_ewma_c[axis],
                EWMA_ALPHA_GYRO_PERCENT);
        }

        /* Accelerometer filtered readings are in meters per second squared. */
        float accel_mps2[3] = {
            accel_ewma_asm[0] * (9.80665f / 1000.0f),
            accel_ewma_asm[1] * (9.80665f / 1000.0f),
            accel_ewma_asm[2] * (9.80665f / 1000.0f)
        };

        /* Gyroscope filtered readings are in degrees per second. */
        float gyro_dps[3] = {
            gyro_ewma_asm[0] / 1000.0f,
            gyro_ewma_asm[1] / 1000.0f,
            gyro_ewma_asm[2] / 1000.0f
        };

        float accel_magnitude =
            sqrtf(accel_mps2[0] * accel_mps2[0] +
                  accel_mps2[1] * accel_mps2[1] +
                  accel_mps2[2] * accel_mps2[2]);

        float gyro_magnitude =
            sqrtf(gyro_dps[0] * gyro_dps[0] +
                  gyro_dps[1] * gyro_dps[1] +
                  gyro_dps[2] * gyro_dps[2]);

        /* Optional debugging check. This confirms that the assembly routine
         * matches the reference C routine for the current samples. */
        if ((accel_ewma_asm[0] != accel_ewma_c[0]) ||
            (accel_ewma_asm[1] != accel_ewma_c[1]) ||
            (accel_ewma_asm[2] != accel_ewma_c[2]) ||
            (gyro_ewma_asm[0] != gyro_ewma_c[0]) ||
            (gyro_ewma_asm[1] != gyro_ewma_c[1]) ||
            (gyro_ewma_asm[2] != gyro_ewma_c[2]))
        {
            mismatch_samples++;
        }

        /**************** Elderly wearable state logic starts here************************
         * Compulsory requirements:
         * 1. Use filtered accelerometer AND gyroscope readings.
         * 2. Distinguish normal activity, near-fall movements, and a real fall.
         * 3. Use a slow LED blink for normal operation and a fast blink after
         *    a fall is detected.
         *********************************************************************/

        uint32_t now = HAL_GetTick();
        FallState previous_state = fall_state;
        FallDetector_Update(accel_magnitude, gyro_magnitude, now);
        int fall_detected = (fall_state == FD_FALL_LATCHED);

        if (previous_state != fall_state) {
            char event[64];
            snprintf(event, sizeof(event), "EVENT t=%lu state=%s\r\n",
                     (unsigned long)now, FallState_Name());
            UART_Send(event);
            if (fall_detected || previous_state == FD_FALL_LATCHED) {
                BSP_LED_On(LED2);
                last_led_toggle = now;
            }
        }

        uint32_t led_delay =
            fall_detected ? FALL_LED_DELAY_MS : NORMAL_LED_DELAY_MS;

        if ((now - last_led_toggle) >= led_delay)
        {
            BSP_LED_Toggle(LED2);
            last_led_toggle = now;
        }

        if ((uint32_t)(now - last_uart_print) >= UART_PRINT_INTERVAL_MS) {
            last_uart_print = now;
            char buffer[320];
            snprintf(buffer, sizeof(buffer),
                     "n=%lu S=%s dtMax=%lu err=%lu "
                     "Ar=%d,%d,%d Af=%d,%d,%d "
                     "Gr=%d,%d,%d Gf=%d,%d,%d A_mg=%d G_dps=%d\r\n",
                     sample_number, FallState_Name(),
                     (unsigned long)max_sample_dt, (unsigned long)mismatch_samples,
                     (int)accel_raw_i16[0], (int)accel_raw_i16[1], (int)accel_raw_i16[2],
                     accel_ewma_asm[0], accel_ewma_asm[1], accel_ewma_asm[2],
                     gyro_raw_int[0], gyro_raw_int[1], gyro_raw_int[2],
                     gyro_ewma_asm[0], gyro_ewma_asm[1], gyro_ewma_asm[2],
                     (int)(accel_magnitude * 1000.0f / 9.80665f), (int)gyro_magnitude);
            UART_Send(buffer);
            max_sample_dt = 0;
        }
        sample_number++;
    }
}

int ewma_filter_C(int new_data, int old_output, int alpha_percent)
{
    /* Reference implementation for verification only. The assembly routine
     * must be used in the actual sensor-processing and detection pipeline. */
    int numerator = alpha_percent * new_data
                  + (100 - alpha_percent) * old_output;
    return numerator / 100;
}

static void UART_Send(const char *text)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)text, strlen(text), HAL_MAX_DELAY);
}

static void UART1_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        while (1) { }
    }
}

/* Do not modify these lines. They suppress UART-related warnings. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}
int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }
int _fstat(int file, struct stat *st) { (void)file; (void)st; return 0; }
int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
int _isatty(int file) { (void)file; return 1; }
int _close(int file) { (void)file; return -1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
