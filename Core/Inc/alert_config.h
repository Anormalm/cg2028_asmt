#ifndef ALERT_CONFIG_H
#define ALERT_CONFIG_H
/* Copy alert_secrets.example.h to alert_secrets.h (ignored by Git). */
#if defined(__has_include)
#if __has_include("alert_secrets.h")
#include "alert_secrets.h"
#endif
#endif
#ifndef ALERT_WIFI_SSID
#define ALERT_WIFI_SSID ""
#endif
#ifndef ALERT_WIFI_PASSWORD
#define ALERT_WIFI_PASSWORD ""
#endif
#ifndef ALERT_SERVER_IP
#define ALERT_SERVER_IP {192, 168, 1, 100}
#endif
#ifndef ALERT_SERVER_PORT
#define ALERT_SERVER_PORT 8080
#endif
#ifndef ALERT_TOKEN
#define ALERT_TOKEN ""
#endif
#ifndef ALERT_DEVICE_ID
#define ALERT_DEVICE_ID "eldercare-01"
#endif
#define ALERT_QUEUE_LENGTH 16
#define ALERT_HEARTBEAT_MS 10000U
#endif
