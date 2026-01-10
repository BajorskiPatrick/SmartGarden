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

// Wysyłanie telemetrii z maską pól (pozostałe pola będą ustawione na null)
void mqtt_app_send_telemetry_masked(telemetry_data_t *data, telemetry_fields_mask_t fields_mask);

// Wysyłanie alertu (QoS 2)
void mqtt_app_send_alert(const char* type, const char* message);

// Sprawdzenie stanu połączenia
bool mqtt_app_is_connected(void);

// Publikuje informacje o tym jakie pola są mierzone (retained)
void mqtt_app_publish_capabilities(void);

#endif // MQTT_APP_H
