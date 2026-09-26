/******************************************************************************
 * @file           : main.c
 * @brief          : CG2028 Assignment - ElderCare Wearable Safety Companion
 * @author         : Hou Linxin
 * (c) CG2028 Teaching Team
 ******************************************************************************/

/*--------------------------- Includes ---------------------------------------*/
#include "main.h"
#include "fall_detector.h"
#include "alert_ui.h"
#include "alert_network.h"
#include "motion_capture.h"
#include "FreeRTOS.h"
#include "task.h"
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

static FallDetector detector;
static AlertUI alert_ui;
volatile int app_scheduler_running;
static void SensorTask(void *argument);

static void SystemClock_Config(void)
{
    /* MSI 4 MHz -> PLL -> 80 MHz. Leaves CPU headroom for RTOS and telemetry. */
    __HAL_RCC_PWR_CLK_ENABLE();
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        App_Fatal();
    RCC_OscInitTypeDef oscillator = {0};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    oscillator.MSIState = RCC_MSI_ON;
    oscillator.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    oscillator.MSIClockRange = RCC_MSIRANGE_6;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    oscillator.PLL.PLLM = 1;
    oscillator.PLL.PLLN = 40;
    oscillator.PLL.PLLP = RCC_PLLP_DIV7;
    oscillator.PLL.PLLQ = RCC_PLLQ_DIV2;
    oscillator.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) App_Fatal();
    RCC_ClkInitTypeDef clock = {0};
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4) != HAL_OK) App_Fatal();
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    UART1_Init();
    BSP_LED_Init(LED2);
    BSP_LED_Off(LED2);
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_GPIO);
    if (BSP_ACCELERO_Init() != ACCELERO_OK || BSP_GYRO_Init() != GYRO_OK) {
        UART_Send("ERROR: motion sensor initialization failed\r\n");
        BSP_LED_On(LED2);
        while (1) { HAL_Delay(100); }
    }
    UART_Send("ElderCare: 50Hz; Ar/Af=mg Gr/Gf=mdps; B2: hold 3s for SOS, release+hold 1s to ACK; buzzer D6\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(ARD_D6_GPIO_Port, ARD_D6_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef buzzer = {0};
    buzzer.Pin = ARD_D6_Pin;
    buzzer.Mode = GPIO_MODE_OUTPUT_PP;
    buzzer.Pull = GPIO_NOPULL;
    buzzer.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ARD_D6_GPIO_Port, &buzzer);
    if (!AlertNetwork_Init() ||
        xTaskCreate(SensorTask, "sensors", 1536, NULL, 3, NULL) != pdPASS ||
        xTaskCreate(AlertNetwork_Task, "network", 2048, NULL, 1, NULL) != pdPASS)
        App_Fatal();
    vTaskStartScheduler();
    App_Fatal();
    return 0;
}

void App_Fatal(void)
{
    __disable_irq();
    BSP_LED_On(LED2);
    HAL_GPIO_WritePin(ARD_D6_GPIO_Port, ARD_D6_Pin, GPIO_PIN_RESET);
    for (;;) { }
}

void vApplicationMallocFailedHook(void) { App_Fatal(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name; App_Fatal();
}

static void SensorTask(void *argument)
{
    (void)argument;
    app_scheduler_running = 1;
    uint32_t incident = 0;
    int capture_armed = 1;
    uint32_t capture_quiet = 0;
    TickType_t wake = xTaskGetTickCount();
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
    FallDetector_Init(&detector, HAL_GetTick());

    while (1)
    {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        uint32_t sample_tick = HAL_GetTick();
        uint32_t sample_dt = (uint32_t)(sample_tick - last_sample);
        if (sample_dt > FD_MAX_SAMPLE_GAP_MS) {
            alert_ui.holding = alert_ui.sos_armed = 0;
            wake = xTaskGetTickCount(); /* Do not replay a backlog of stale samples. */
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
        FallState previous_state = detector.state;
        int pressed = BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_RESET;
        FallDetector_Update(&detector, accel_mps2, accel_magnitude, gyro_magnitude,
                            pressed, now);
        int manual_sos = AlertUI_Update(&alert_ui, now, pressed,
            detector.state == FD_NORMAL, detector.state == FD_FALL_LATCHED);
        if (manual_sos) {
            detector.state = FD_FALL_LATCHED;
            detector.state_since = now;
            detector.button_tracking = detector.ack_armed = 0;
            detector.reason = "manual_sos";
            detector.min_accel = detector.peak_accel = accel_magnitude;
            detector.peak_gyro = gyro_magnitude;
            AlertUI_Update(&alert_ui, now, pressed, 0, 1);
            alert_ui.sos_pattern = 1;
        }
        MotionSample motion = {0};
        motion.time_ms = now;
        motion.state = detector.state;
        motion.flags = (detector.low_g_seen ? 1U : 0U) |
            (detector.rotation_seen ? 2U : 0U) | (detector.reference_valid ? 4U : 0U) |
            (detector.quiet_tracking ? 8U : 0U) | (detector.posture_tracking ? 16U : 0U) |
            (sample_dt > FD_MAX_SAMPLE_GAP_MS ? 64U : 0U);
        int64_t raw_accel_sq = 0, raw_gyro_sq = 0;
        for (int i = 0; i < 3; ++i) {
            motion.ar[i] = accel_raw_i16[i]; motion.af[i] = accel_ewma_asm[i];
            motion.gr[i] = gyro_raw_int[i]; motion.gf[i] = gyro_ewma_asm[i];
            raw_accel_sq += (int64_t)motion.ar[i] * motion.ar[i];
            raw_gyro_sq += (int64_t)motion.gr[i] * motion.gr[i];
            if (motion.ar[i] >= 1950 || motion.ar[i] <= -1950) motion.flags |= 32U;
        }
        int candidate_started = previous_state == FD_NORMAL &&
            (detector.state == FD_WAIT_IMPACT || detector.state == FD_CONFIRM);
        const char *capture_trigger = NULL;
        if (debug_capture_request) { capture_trigger = "manual"; debug_capture_request = 0; }
        else if (capture_armed && candidate_started) capture_trigger = "candidate";
        else if (capture_armed && detector.state == FD_NORMAL &&
            (raw_accel_sq < 250000LL || raw_accel_sq > 3240000LL || raw_gyro_sq > 22500000000LL))
            capture_trigger = "raw_motion";
        if (capture_trigger) { capture_armed = 0; capture_quiet = 0; }
        if (accel_magnitude > 0.85f * FD_GRAVITY && accel_magnitude < 1.15f * FD_GRAVITY && gyro_magnitude < 20.0f) {
            if (++capture_quiet >= 50) capture_armed = 1;
        } else capture_quiet = 0;
        const char *capture_outcome = NULL;
        if (previous_state != detector.state && previous_state != FD_FALL_LATCHED &&
            (detector.state == FD_FALL_LATCHED ||
             ((previous_state == FD_CONFIRM || previous_state == FD_WAIT_IMPACT) && detector.state == FD_STARTUP)))
            capture_outcome = detector.reason;
        MotionCapture_Add(&motion, capture_trigger, capture_outcome);
        int fall_detected = (detector.state == FD_FALL_LATCHED);
        HAL_GPIO_WritePin(ARD_D6_GPIO_Port, ARD_D6_Pin,
                         AlertUI_BuzzerOn(&alert_ui, now) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        AlertEvent message = {0};
        message.uptime_ms = now;
        message.incident = incident;
        message.accel_mg = (int)(accel_magnitude * 1000.0f / FD_GRAVITY);
        message.gyro_dps = (int)gyro_magnitude;
        message.min_mg = (int)(detector.min_accel * 1000.0f / FD_GRAVITY);
        message.peak_mg = (int)(detector.peak_accel * 1000.0f / FD_GRAVITY);
        message.peak_dps = (int)detector.peak_gyro;
        message.rejection_flags = detector.rejection_flags;
        snprintf(message.state, sizeof(message.state), "%s", FallState_Name(detector.state));
        snprintf(message.reason, sizeof(message.reason), "%s", detector.reason);
        if (fall_detected && previous_state != FD_FALL_LATCHED) {
            strcpy(message.type, manual_sos ? "sos" : "fall");
            message.incident = 0;
            incident = AlertNetwork_Publish(&message);
        } else if (!fall_detected && previous_state == FD_FALL_LATCHED) {
            strcpy(message.type, "local_ack");
            AlertNetwork_Publish(&message);
            incident = 0;
        }
        if ((previous_state == FD_CONFIRM || previous_state == FD_WAIT_IMPACT) && detector.state == FD_STARTUP) {
            strcpy(message.type, "rejected");
            message.incident = 0;
            AlertNetwork_Publish(&message);
        }
        message.incident = incident;
        AlertNetwork_SetSnapshot(&message);

        if (previous_state != detector.state) {
            char event[160];
            snprintf(event, sizeof(event),
                     "EVENT t=%lu state=%s why=%s min_mg=%d peak_mg=%d peak_dps=%d reject=%lu\r\n",
                     (unsigned long)now, FallState_Name(detector.state), detector.reason,
                     (int)(detector.min_accel * 1000.0f / FD_GRAVITY),
                     (int)(detector.peak_accel * 1000.0f / FD_GRAVITY),
                     (int)detector.peak_gyro, (unsigned long)detector.rejection_flags);
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
                     "n=%lu S=%s dtMax=%lu err=%lu net=%d delivered=%lu dropped=%lu "
                     "Ar=%d,%d,%d Af=%d,%d,%d "
                     "Gr=%d,%d,%d Gf=%d,%d,%d A_mg=%d G_dps=%d\r\n",
                     sample_number, FallState_Name(detector.state),
                     (unsigned long)max_sample_dt, (unsigned long)mismatch_samples,
                     (int)AlertNetwork_State(), (unsigned long)AlertNetwork_Delivered(),
                     (unsigned long)AlertNetwork_Dropped(),
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
    int64_t numerator = (int64_t)alpha_percent * new_data
                      + (int64_t)(100 - alpha_percent) * old_output;
    return (int)(numerator / 100);
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
