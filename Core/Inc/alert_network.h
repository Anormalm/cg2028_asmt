#ifndef ALERT_NETWORK_H
#define ALERT_NETWORK_H
#include <stdint.h>
typedef struct {
    uint32_t sequence, incident, uptime_ms, dropped;
    int accel_mg, gyro_dps, min_mg, peak_mg, peak_dps;
    char type[16], state[16], reason[24];
} AlertEvent;
typedef enum { NET_DISABLED, NET_CONNECTING, NET_ONLINE, NET_RETRY } NetworkState;
int AlertNetwork_Init(void);
uint32_t AlertNetwork_Publish(AlertEvent *event);
void AlertNetwork_SetSnapshot(const AlertEvent *event);
NetworkState AlertNetwork_State(void);
uint32_t AlertNetwork_Delivered(void);
uint32_t AlertNetwork_Dropped(void);
void AlertNetwork_Task(void *argument);
#endif
