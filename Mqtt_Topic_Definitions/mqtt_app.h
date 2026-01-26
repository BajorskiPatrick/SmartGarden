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
// Wysyłanie telemetrii z maską pól (pozostałe pola będą ustawione na null)
void mqtt_app_send_telemetry_masked(telemetry_data_t *data, telemetry_fields_mask_t fields_mask);

// Zwraca ilosc zbuforowanych pakietow z rzedu
int mqtt_app_get_consecutive_buffered_count(void);

// Wysyłanie alertu (QoS 2)
void mqtt_app_send_alert(const char* type, const char* message);

// Sprawdzenie stanu połączenia
bool mqtt_app_is_connected(void);

// Publikuje informacje o tym jakie pola są mierzone (retained)
void mqtt_app_publish_capabilities(void);

// --- Alerty v2 (zalecane) ---
// Publikuje alert na topic /alert. Dla kompatybilności backend może dalej czytać pola `type` i `msg`.
// `severity`: "debug" | "info" | "warning" | "error" | "critical"
// `subsystem`: np. "wifi", "mqtt", "sensor", "thresholds", "command", "system", "telemetry"
void mqtt_app_send_alert2(const char* code, const char* severity, const char* subsystem, const char* message);

// `details_json` powinien być JSON-em typu object (np. {"reason":201,"suppressed":3}), albo NULL.
void mqtt_app_send_alert2_details(const char* code, const char* severity, const char* subsystem, const char* message, const char* details_json);

// Publikuje wiadomość na podścieżkę (np. "settings/state") względem garden/{user}/{device}/...
void mqtt_app_publish_to_subpath(const char* subpath, const char* data, int qos);

#endif // MQTT_APP_H
