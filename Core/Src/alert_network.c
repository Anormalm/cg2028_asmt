#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "alert_network.h"
#include "alert_config.h"
#include "alert_protocol.h"
#include "motion_capture.h"
#include "extra_sensors.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

//timing:
#define RETRY_START_MS          1000U   // first retry delay after a failure
#define RETRY_MAX_MS           30000U   // backoff caps out here
#define SENSOR_UPLOAD_MS         200U   // how often to push sound readings
#define LOOP_DELAY_MS             20U
#define SEND_TIMEOUT_MS         1500U
#define REPLY_WINDOW_MS         5000U   // total time we wait for the server reply
#define RECEIVE_TIMEOUT_MS       500U   // timeout for each individual read
#define IDLE_CHECK_MS          10000U   // delay loop when networking is disabled
#define SOCKET_ID                  0

/* Which step of a send we are on. */
typedef enum {
    NET_STEP_IDLE,
    NET_STEP_MODULE_INIT,
    NET_STEP_JOIN,
    NET_STEP_SOCKET,
    NET_STEP_SEND,
    NET_STEP_RECEIVE,
    NET_STEP_ACK
} NetworkStep;

/*--------------------------- State ------------------------------------------*/
static QueueHandle_t events;            // alarms waiting to be sent
static AlertEvent snapshot;             // latest state from the sensor task, used for heartbeats
static uint32_t next_sequence;
static uint32_t dropped;                // alarms lost because the queue was full
static volatile uint32_t delivered;     // sequence number of the last acked event
static volatile NetworkState network_state = NET_DISABLED;

static char boot_id[17];                // 16 hex chars + '\0'
static int socket_open;
static uint8_t server_ip[4] = ALERT_SERVER_IP;

/* Left global (not static) on purpose so we can watch them in Live
 * Expressions when debugging the WiFi connection. */
volatile NetworkStep network_step = NET_STEP_IDLE;
volatile NetworkStep network_failed_step = NET_STEP_IDLE;
volatile int network_last_status;
volatile unsigned int network_http_status;

// remember which step failed and the error code for debugging
static void NetworkFailure(NetworkStep step, int status)
{
    network_failed_step = step;
    network_last_status = status;
}

// exponential backoff: 1s, 2s, 4s ... up to 30s
static uint32_t NextRetryDelay(uint32_t retry_ms)
{
    if (retry_ms >= RETRY_MAX_MS) return retry_ms;
    return (retry_ms * 2U > RETRY_MAX_MS) ? RETRY_MAX_MS : retry_ms * 2U;
}

// sequence numbers are shared by alarms, heartbeats and sensor posts
static uint32_t NextSequence(void)
{
    uint32_t value;
    taskENTER_CRITICAL();
    value = ++next_sequence;
    taskEXIT_CRITICAL();
    return value;
}

static int IsDigit(char c) { return c >= '0' && c <= '9'; }

// pull the 3 digit status code out of "HTTP/1.x NNN" once we have enough bytes
static void UpdateHttpStatus(const char *response, size_t used)
{
    if (used >= 12 && !strncmp(response, "HTTP/1.", 7) &&
        IsDigit(response[9]) && IsDigit(response[10]) && IsDigit(response[11])) {
        network_http_status = (response[9] - '0') * 100 +
                              (response[10] - '0') * 10 +
                              (response[11] - '0');
    }
}

int AlertNetwork_Init(void)
{
    events = xQueueCreate(ALERT_QUEUE_LENGTH, sizeof(AlertEvent));
    return events != NULL;
}

/* Called from the sensor task. Never blocks - if the queue is full we just
 * count it as dropped so the server can see something was lost. */
uint32_t AlertNetwork_Publish(AlertEvent *event)
{
    event->sequence = NextSequence();
    if (!event->incident) event->incident = event->sequence;   // new incident starts at its own seq
    event->dropped = dropped;

    if (xQueueSend(events, event, 0) != pdPASS) {
        taskENTER_CRITICAL();
        ++dropped;
        taskEXIT_CRITICAL();
    }
    return event->sequence;
}

void AlertNetwork_SetSnapshot(const AlertEvent *event)
{
    taskENTER_CRITICAL();
    snapshot = *event;
    taskEXIT_CRITICAL();
}

NetworkState AlertNetwork_State(void) { return network_state; }
uint32_t AlertNetwork_Delivered(void) { return delivered; }

uint32_t AlertNetwork_Dropped(void)
{
    uint32_t result;
    taskENTER_CRITICAL();
    result = dropped;
    taskEXIT_CRITICAL();
    return result;
}

/*--------------------------- Setup checks -----------------------------------*/
static int BootIdentity(void)
{
    // RNG needs the 48 MHz HSI clock
    RCC_OscInitTypeDef oscillator = {0};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    oscillator.HSI48State = RCC_HSI48_ON;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) return 0;

    RCC_PeriphCLKInitTypeDef clock = {0};
    clock.PeriphClockSelection = RCC_PERIPHCLK_RNG;
    clock.RngClockSelection = RCC_RNGCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return 0;
    __HAL_RCC_RNG_CLK_ENABLE();

    RNG_HandleTypeDef rng = {0};
    rng.Instance = RNG;
    uint32_t a, b;
    if (HAL_RNG_Init(&rng) != HAL_OK ||
        HAL_RNG_GenerateRandomNumber(&rng, &a) != HAL_OK ||
        HAL_RNG_GenerateRandomNumber(&rng, &b) != HAL_OK) {
        return 0;
    }
    HAL_RNG_DeInit(&rng);   // only needed once

    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx", (unsigned long)a, (unsigned long)b);
    return 1;
}

/* Sanity check alert_config.h so we don't send garbage (or the placeholder
 * token) to the server. Device ID must be simple chars since it goes
 * straight into the JSON without escaping. */
static int ValidConfig(void)
{
    size_t id_len = strlen(ALERT_DEVICE_ID);
    size_t token_len = strlen(ALERT_TOKEN);
    size_t ssid_len = strlen(ALERT_WIFI_SSID);

    if (id_len == 0 || id_len > 32) return 0;
    if (token_len < 16 || token_len > 64) return 0;
    if (ssid_len == 0 || ssid_len > 32) return 0;
    if (strlen(ALERT_WIFI_PASSWORD) > 32) return 0;

    for (const char *p = ALERT_DEVICE_ID; *p; ++p) {
        int ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 IsDigit(*p) || *p == '-' || *p == '_';
        if (!ok) return 0;
    }
    // token goes in a header, so printable ASCII only
    for (const char *p = ALERT_TOKEN; *p; ++p)
        if (*p <= ' ' || *p > '~') return 0;

    return strstr(ALERT_TOKEN, "REPLACE_") == NULL;
}

static int PostPayload(const char *path, const char *body, const char *expected, int sensor_reply)
{
    char request[1300];
    char response[512] = {0};
    int body_length = (int)strlen(body);

    int length = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %u.%u.%u.%u:%u\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: keep-alive\r\n\r\n%s",
        path, server_ip[0], server_ip[1], server_ip[2], server_ip[3], ALERT_SERVER_PORT,
        ALERT_TOKEN, body_length, body);
    if (length < 0 || length >= (int)sizeof(request)) return 0;   // didn't fit

    network_http_status = 0;

    // open the socket only if we don't already have one
    network_step = NET_STEP_SOCKET;
    WIFI_Status_t status = WIFI_STATUS_OK;
    if (!socket_open) {
        status = WIFI_OpenClientConnection(SOCKET_ID, WIFI_TCP_PROTOCOL, "care",
                                           server_ip, ALERT_SERVER_PORT, 0);
    }
    if (status != WIFI_STATUS_OK) {
        NetworkFailure(network_step, status);
        return 0;
    }
    socket_open = 1;

    uint16_t sent = 0;
    int success = 0;
    network_step = NET_STEP_SEND;
    status = WIFI_SendData(SOCKET_ID, (uint8_t *)request, (uint16_t)length, &sent, SEND_TIMEOUT_MS);

    if (status == WIFI_STATUS_OK && sent == length) {
        size_t used = 0;
        uint32_t start = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - start) < REPLY_WINDOW_MS &&
               used < sizeof(response) - 1) {
            uint16_t got = 0;
            network_step = NET_STEP_RECEIVE;
            status = WIFI_ReceiveData(SOCKET_ID, (uint8_t *)response + used,
                                      (uint16_t)(sizeof(response) - 1 - used),
                                      &got, RECEIVE_TIMEOUT_MS);
            if (status != WIFI_STATUS_OK) {
                NetworkFailure(network_step, status);
                break;
            }
            used += got;
            response[used] = '\0';
            UpdateHttpStatus(response, used);

            network_step = NET_STEP_ACK;
            int acked = sensor_reply ? ExtraSensors_ParseReply(response, expected)
                                     : AlertProtocol_IsAck(response, expected);
            if (acked) {
                success = 1;
                break;
            }
            if (!got) vTaskDelay(pdMS_TO_TICKS(20));   // nothing yet, don't spin
        }
    }

    // receive errors are already logged above, -1 means timed out / no ACK
    if (!success && network_step != NET_STEP_RECEIVE)
        NetworkFailure(network_step, status == WIFI_STATUS_OK ? -1 : (int)status);

    // drop the socket on failure or if the server doesn't want keep-alive
    if (!success || strstr(response, "Connection: close") || !strncmp(response, "HTTP/1.0", 8)) {
        WIFI_CloseClientConnection(SOCKET_ID);
        socket_open = 0;
    }
    return success;
}

// fall / sos / local_ack / rejected / heartbeat events
static int PostEvent(const AlertEvent *event)
{
    char body[600];
    char expected[64];

    int body_length = snprintf(body, sizeof(body),
        "{\"device\":\"%s\",\"boot\":\"%s\",\"seq\":%lu,\"incident\":%lu,"
        "\"type\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\","
        "\"uptime_ms\":%lu,\"accel_mg\":%d,\"gyro_dps\":%d,"
        "\"min_mg\":%d,\"peak_mg\":%d,\"peak_dps\":%d,"
        "\"dropped\":%lu,\"rejection_flags\":%lu}",
        ALERT_DEVICE_ID, boot_id,
        (unsigned long)event->sequence, (unsigned long)event->incident,
        event->type, event->state, event->reason,
        (unsigned long)event->uptime_ms, event->accel_mg, event->gyro_dps,
        event->min_mg, event->peak_mg, event->peak_dps,
        (unsigned long)event->dropped, (unsigned long)event->rejection_flags);
    if (body_length < 0 || body_length >= (int)sizeof(body)) return 0;

    snprintf(expected, sizeof(expected), "ACK %s-%lu\n", boot_id, (unsigned long)event->sequence);
    return PostPayload("/api/events", body, expected, 0);
}

/* Uploads one chunk of a finished motion capture. *part tracks which chunk
 * we're on and goes back to 0 once the whole capture is sent.
 * Returns -1 if there's nothing to upload, 0 on failure, 1 on success. */
static int PostCapture(uint32_t *part)
{
    CaptureInfo info;
    MotionSample rows[CAPTURE_CHUNK];
    uint32_t count;
    if (!MotionCapture_Read(*part, &info, rows, &count)) return -1;

    char body[1000];
    char expected[80];
    int used = snprintf(body, sizeof(body),
        "{\"device\":\"%s\",\"boot\":\"%s\",\"capture_id\":%lu,\"part\":%lu,"
        "\"total\":%lu,\"pre_count\":%lu,\"trigger_ms\":%lu,"
        "\"source\":\"%s\",\"outcome\":\"%s\",\"samples\":[",
        ALERT_DEVICE_ID, boot_id, (unsigned long)info.id, (unsigned long)*part,
        (unsigned long)info.count, (unsigned long)info.pre_count,
        (unsigned long)info.trigger_ms, info.source, info.outcome);

    // each sample is one array: [time, raw accel xyz, filtered accel xyz,
    //                            raw gyro xyz, filtered gyro xyz, state, flags]
    for (uint32_t i = 0; i < count; ++i) {
        const MotionSample *r = &rows[i];
        if (used < 0 || used >= (int)sizeof(body)) return 0;
        int wrote = snprintf(body + used, sizeof(body) - used,
            "%s[%lu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu,%lu]",
            i ? "," : "", (unsigned long)r->time_ms,
            (long)r->ar[0], (long)r->ar[1], (long)r->ar[2],
            (long)r->af[0], (long)r->af[1], (long)r->af[2],
            (long)r->gr[0], (long)r->gr[1], (long)r->gr[2],
            (long)r->gf[0], (long)r->gf[1], (long)r->gf[2],
            (unsigned long)r->state, (unsigned long)r->flags);
        if (wrote < 0) return 0;
        used += wrote;
    }
    if (used + 3 >= (int)sizeof(body)) return 0;
    strcpy(body + used, "]}");

    snprintf(expected, sizeof(expected), "ACK %s-c%lu-%lu\n", boot_id,
             (unsigned long)info.id, (unsigned long)*part);
    if (!PostPayload("/api/captures", body, expected, 0)) return 0;

    // last chunk sent -> free the slot for the next capture
    if (++*part * CAPTURE_CHUNK >= info.count) {
        MotionCapture_Release(info.id);
        *part = 0;
    }
    return 1;
}

// microphone readings; the reply may carry a new sound config
static int PostSensors(void)
{
    SensorSnapshot s;
    ExtraSensors_Get(&s);
    uint32_t seq = NextSequence();
    char body[650];
    char expected[64];

    int n = snprintf(body, sizeof(body),
        "{\"device\":\"%s\",\"boot\":\"%s\",\"seq\":%lu,\"uptime_ms\":%lu,"
        "\"config_revision\":%lu,"
        "\"sound_dbfs\":%d,\"sound_valid\":%d,\"sound_active\":%d,\"sound_masked\":%d,"
        "\"sound_events\":%lu,\"audio_overruns\":%lu,\"mic_error\":%d}",
        ALERT_DEVICE_ID, boot_id, (unsigned long)seq, (unsigned long)s.uptime_ms,
        (unsigned long)s.config_revision,
        s.sound_dbfs, s.sound_valid, s.sound_active, s.sound_masked,
        (unsigned long)s.sound_events, (unsigned long)s.audio_overruns, s.mic_error);
    if (n < 0 || n >= (int)sizeof(body)) return 0;

    snprintf(expected, sizeof(expected), "ACK %s-s%lu\n", boot_id, (unsigned long)seq);
    return PostPayload("/api/sensors", body, expected, 1);
}

/*--------------------------- Task -------------------------------------------*/

/* Priority order each loop:
 *   1. queued alarms (fall/sos/ack/rejected) - always first
 *   2. heartbeat when it's due
 *   3. otherwise take turns between sound readings and capture uploads so
 *      one doesn't starve the other
 */
void AlertNetwork_Task(void *argument)
{
    (void)argument;

    // bad config or no RNG -> stay offline forever, the rest of the board still runs
    if (!ValidConfig() || !BootIdentity()) {
        network_state = NET_DISABLED;
        for (;;) vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_MS));
    }

    int connected = 0;
    uint32_t capture_part = 0;
    uint32_t sensors_at = 0;
    int sensor_turn = 1;
    uint32_t retry_ms = RETRY_START_MS;
    uint32_t heartbeat_at = HAL_GetTick();

    for (;;) {
        // (re)join the WiFi if we lost it
        if (!connected) {
            network_state = NET_CONNECTING;
            network_step = NET_STEP_MODULE_INIT;
            WIFI_Status_t status = WIFI_Init();
            if (status == WIFI_STATUS_OK) {
                network_step = NET_STEP_JOIN;
                status = WIFI_Connect(ALERT_WIFI_SSID, ALERT_WIFI_PASSWORD, WIFI_ECN_WPA2_PSK);
            }
            connected = (status == WIFI_STATUS_OK);
            if (!connected) {
                NetworkFailure(network_step, status);
                network_state = NET_RETRY;
                vTaskDelay(pdMS_TO_TICKS(retry_ms));
                retry_ms = NextRetryDelay(retry_ms);
                continue;
            }
        }

        // peek, not receive - the alarm only leaves the queue once it's acked
        AlertEvent event;
        int queued = xQueuePeek(events, &event, 0) == pdPASS;
        int send_heartbeat = !queued &&
            (uint32_t)(HAL_GetTick() - heartbeat_at) >= ALERT_HEARTBEAT_MS;
        int send_sensors = !queued && !send_heartbeat &&
            (uint32_t)(HAL_GetTick() - sensors_at) >= SENSOR_UPLOAD_MS;
        int result;

        if (send_sensors && sensor_turn) {
            sensor_turn = 0;
            sensors_at = HAL_GetTick();
            result = PostSensors();
        } else if (!queued && !send_heartbeat) {
            sensor_turn = 1;
            result = PostCapture(&capture_part);
            if (result < 0) {   // no capture waiting, nothing to do
                vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
                continue;
            }
        } else {
            if (send_heartbeat) {
                // heartbeat = latest snapshot with a fresh sequence number
                taskENTER_CRITICAL();
                event = snapshot;
                taskEXIT_CRITICAL();
                event.sequence = NextSequence();
                event.dropped = AlertNetwork_Dropped();
                strcpy(event.type, "heartbeat");
                heartbeat_at = HAL_GetTick();
            }
            result = PostEvent(&event);
        }

        if (result) {
            network_state = NET_ONLINE;
            network_step = NET_STEP_IDLE;
            if (queued || send_heartbeat) delivered = event.sequence;
            retry_ms = RETRY_START_MS;
            if (queued) xQueueReceive(events, &event, 0);   // acked, now remove it
        } else {
            /* Alarm stays in the queue with the same sequence number, so the
             * retry is the same event and the server can dedupe it. */
            network_state = NET_RETRY;
            connected = 0;
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
            retry_ms = NextRetryDelay(retry_ms);
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}
