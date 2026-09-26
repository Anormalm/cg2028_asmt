#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "alert_network.h"
#include "alert_config.h"
#include "alert_protocol.h"
#include "motion_capture.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

static QueueHandle_t events;
static AlertEvent snapshot;
static uint32_t next_sequence, dropped;
static volatile uint32_t delivered;
static volatile NetworkState network_state = NET_DISABLED;
typedef enum {
    NET_STEP_IDLE, NET_STEP_MODULE_INIT, NET_STEP_JOIN,
    NET_STEP_SOCKET, NET_STEP_SEND, NET_STEP_RECEIVE, NET_STEP_ACK
} NetworkStep;
volatile NetworkStep network_step = NET_STEP_IDLE;
volatile NetworkStep network_failed_step = NET_STEP_IDLE;
volatile int network_last_status;
volatile unsigned int network_http_status;

static void NetworkFailure(NetworkStep step, int status)
{
    network_failed_step = step;
    network_last_status = status;
}

static char boot_id[17];
static uint8_t server_ip[4] = ALERT_SERVER_IP;

int AlertNetwork_Init(void)
{
    events = xQueueCreate(ALERT_QUEUE_LENGTH, sizeof(AlertEvent));
    return events != NULL;
}

static uint32_t NextSequence(void)
{
    uint32_t value;
    taskENTER_CRITICAL();
    value = ++next_sequence;
    taskEXIT_CRITICAL();
    return value;
}

uint32_t AlertNetwork_Publish(AlertEvent *event)
{
    event->sequence = NextSequence();
    if (!event->incident) event->incident = event->sequence;
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
    taskENTER_CRITICAL(); result = dropped; taskEXIT_CRITICAL();
    return result;
}

/* Random boot nonce avoids duplicate IDs after reset. If RNG initialization
 * fails, leave remote delivery disabled; local detection and alarm continue.
 */
static int BootIdentity(void)
{
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
        HAL_RNG_GenerateRandomNumber(&rng, &b) != HAL_OK) return 0;
    HAL_RNG_DeInit(&rng);
    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx", (unsigned long)a, (unsigned long)b);
    return 1;
}

static int ValidConfig(void)
{
    size_t n = strlen(ALERT_DEVICE_ID);
    if (!n || n > 32 || strlen(ALERT_TOKEN) < 16 || strlen(ALERT_TOKEN) > 64 ||
        !strlen(ALERT_WIFI_SSID) || strlen(ALERT_WIFI_SSID) > 32 ||
        strlen(ALERT_WIFI_PASSWORD) > 32) return 0;
    for (const char *p = ALERT_DEVICE_ID; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return 0;
    for (const char *p = ALERT_TOKEN; *p; ++p)
        if (*p <= ' ' || *p > '~') return 0;
    return strstr(ALERT_TOKEN, "REPLACE_") == NULL;
}

static int PostPayload(const char *path, const char *body, const char *expected)
{
    char request[1300], response[512];
    int body_length = (int)strlen(body);
    int length = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\n"
        "Authorization: Bearer %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
        path, server_ip[0], server_ip[1], server_ip[2], server_ip[3], ALERT_SERVER_PORT,
        ALERT_TOKEN, body_length, body);
    if (length < 0 || length >= (int)sizeof(request)) return 0;
    network_http_status = 0;
    network_step = NET_STEP_SOCKET;
    WIFI_Status_t status = WIFI_OpenClientConnection(0, WIFI_TCP_PROTOCOL, "care", server_ip,
                                                    ALERT_SERVER_PORT, 0);
    if (status != WIFI_STATUS_OK) { NetworkFailure(network_step, status); return 0; }
    uint16_t sent = 0;
    int success = 0;
    network_step = NET_STEP_SEND;
    status = WIFI_SendData(0, (uint8_t *)request, (uint16_t)length, &sent, 1500);
    if (status == WIFI_STATUS_OK && sent == length) {
        size_t used = 0;
        uint32_t start = HAL_GetTick();
        /* TCP may split headers/body arbitrarily. A send alone is not delivery. */
        while ((uint32_t)(HAL_GetTick() - start) < 5000U && used < sizeof(response)-1) {
            uint16_t got = 0;
            network_step = NET_STEP_RECEIVE;
            status = WIFI_ReceiveData(0, (uint8_t *)response + used,
                (uint16_t)(sizeof(response)-1-used), &got, 500);
            if (status != WIFI_STATUS_OK) { NetworkFailure(network_step, status); break; }
            used += got;
            response[used] = 0;
            if (used >= 12 && !strncmp(response, "HTTP/1.", 7) &&
                response[9] >= '0' && response[9] <= '9' &&
                response[10] >= '0' && response[10] <= '9' &&
                response[11] >= '0' && response[11] <= '9')
                network_http_status = (response[9]-'0')*100 +
                                      (response[10]-'0')*10 + response[11]-'0';
            network_step = NET_STEP_ACK;
            if (AlertProtocol_IsAck(response, expected)) { success = 1; break; }
            if (!got) vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (!success && network_step != NET_STEP_RECEIVE)
        NetworkFailure(network_step, status == WIFI_STATUS_OK ? -1 : (int)status);
    WIFI_CloseClientConnection(0);
    return success;
}

static int PostEvent(const AlertEvent *event)
{
    char body[600], expected[64];
    int body_length = snprintf(body, sizeof(body),
        "{\"device\":\"%s\",\"boot\":\"%s\",\"seq\":%lu,\"incident\":%lu,"
        "\"type\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\","
        "\"uptime_ms\":%lu,\"accel_mg\":%d,\"gyro_dps\":%d,"
        "\"min_mg\":%d,\"peak_mg\":%d,\"peak_dps\":%d,\"dropped\":%lu,\"rejection_flags\":%lu}",
        ALERT_DEVICE_ID, boot_id, (unsigned long)event->sequence,
        (unsigned long)event->incident, event->type, event->state, event->reason,
        (unsigned long)event->uptime_ms, event->accel_mg, event->gyro_dps,
        event->min_mg, event->peak_mg, event->peak_dps, (unsigned long)event->dropped, (unsigned long)event->rejection_flags);
    if (body_length < 0 || body_length >= (int)sizeof(body)) return 0;
    snprintf(expected, sizeof(expected), "ACK %s-%lu\n", boot_id, (unsigned long)event->sequence);
    return PostPayload("/api/events", body, expected);
}

static int PostCapture(uint32_t *part)
{
    CaptureInfo info;
    MotionSample rows[CAPTURE_CHUNK];
    uint32_t count;
    if (!MotionCapture_Read(*part, &info, rows, &count)) return -1;
    char body[1000], expected[80];
    int used = snprintf(body, sizeof(body),
        "{\"device\":\"%s\",\"boot\":\"%s\",\"capture_id\":%lu,\"part\":%lu,"
        "\"total\":%lu,\"pre_count\":%lu,\"trigger_ms\":%lu,\"source\":\"%s\",\"outcome\":\"%s\",\"samples\":[",
        ALERT_DEVICE_ID, boot_id, (unsigned long)info.id, (unsigned long)*part,
        (unsigned long)info.count, (unsigned long)info.pre_count,
        (unsigned long)info.trigger_ms, info.source, info.outcome);
    for (uint32_t i = 0; i < count; ++i) {
        MotionSample *r = &rows[i];
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
    if (!PostPayload("/api/captures", body, expected)) return 0;
    if (++*part * CAPTURE_CHUNK >= info.count) {
        MotionCapture_Release(info.id);
        *part = 0;
    }
    return 1;
}

void AlertNetwork_Task(void *argument)
{
    (void)argument;
    if (!ValidConfig() || !BootIdentity()) {
        network_state = NET_DISABLED;
        for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
    }
    int connected = 0;
    uint32_t capture_part = 0;
    uint32_t retry_ms = 1000U, heartbeat_at = HAL_GetTick();
    for (;;) {
        if (!connected) {
            network_state = NET_CONNECTING;
            network_step = NET_STEP_MODULE_INIT;
            WIFI_Status_t status = WIFI_Init();
            if (status == WIFI_STATUS_OK) {
                network_step = NET_STEP_JOIN;
                status = WIFI_Connect(ALERT_WIFI_SSID, ALERT_WIFI_PASSWORD, WIFI_ECN_WPA2_PSK);
            }
            connected = status == WIFI_STATUS_OK;
            if (!connected) NetworkFailure(network_step, status);
            if (!connected) {
                network_state = NET_RETRY;
                vTaskDelay(pdMS_TO_TICKS(retry_ms));
                if (retry_ms < 30000U) retry_ms = retry_ms * 2 > 30000U ? 30000U : retry_ms * 2;
                continue;
            }
        }
        AlertEvent event;
        int queued = xQueuePeek(events, &event, 0) == pdPASS;
        int send_heartbeat = !queued &&
            (uint32_t)(HAL_GetTick() - heartbeat_at) >= ALERT_HEARTBEAT_MS;
        int result;
        if (!queued && !send_heartbeat) {
            result = PostCapture(&capture_part);
            if (result < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        } else {
            if (send_heartbeat) {
                taskENTER_CRITICAL(); event = snapshot; taskEXIT_CRITICAL();
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
            retry_ms = 1000U;
            if (queued) xQueueReceive(events, &event, 0);
        } else {
            /* Peeked alarm remains queued with its original sequence for retry. */
            network_state = NET_RETRY;
            connected = 0;
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
            if (retry_ms < 30000U) retry_ms = retry_ms * 2 > 30000U ? 30000U : retry_ms * 2;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
