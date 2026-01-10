#ifndef MQTT_APP_H
#define MQTT_APP_H

#include <stdbool.h>
#include "common_defs.h"

// Typ funkcji zwrotnej do obsługi przychodzących wiadomości (komendy, progi)
typedef void (*mqtt_data_callback_t)(const char *topic, const char *payload, int len);

// Start modułu MQTT
void mqtt_app_start(mqtt_data_callback_t cb);

// Wysyłanie telemetrii (obsługuje buforowanie offline)
void mqtt_app_send_telemetry(telemetry_data_t *data);

// Wysyłanie alertu (QoS 2)
void mqtt_app_send_alert(const char* type, const char* message);

// Sprawdzenie stanu połączenia
bool mqtt_app_is_connected(void);

#endif // MQTT_APP_H
