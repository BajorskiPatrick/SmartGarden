#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_PROV_MAX_SSID_LEN      32
#define WIFI_PROV_MAX_PASS_LEN      64
#define WIFI_PROV_MAX_BROKER_LEN    128
#define WIFI_PROV_MAX_MQTT_LOGIN    64
#define WIFI_PROV_MAX_MQTT_PASS     64
#define WIFI_PROV_MAX_USER_ID       64

typedef struct {
	char ssid[WIFI_PROV_MAX_SSID_LEN];
	char pass[WIFI_PROV_MAX_PASS_LEN];
	char broker_uri[WIFI_PROV_MAX_BROKER_LEN];
	char mqtt_login[WIFI_PROV_MAX_MQTT_LOGIN];
	char mqtt_pass[WIFI_PROV_MAX_MQTT_PASS];
	char user_id[WIFI_PROV_MAX_USER_ID];
} wifi_prov_config_t;

/**
 * @brief Inicjalizuje moduł Wi-Fi oraz mechanizm provisioningu (BLE).
 * 
 * Funkcja sprawdza, czy w NVS zapisane są dane logowania do Wi-Fi.
 * - Jeśli TAK: Próbuje się połączyć.
 * - Jeśli NIE: Uruchamia tryb Provisioning przez proste BLE (GATT Server).
 * 
 * Uruchamia również task monitorujący przycisk BOOT (GPIO 0).
 * - Krótkie wciśnięcie (gdy brak WiFi): otwiera okno provisioningu.
 * - Długie wciśnięcie (>3s): Czyści dane WiFi i restartuje układ.
 */
void wifi_prov_init(void);

/**
 * @brief Blokuje wykonanie do momentu nawiązania połączenia WiFi i pobrania IP.
 * 
 * @return true jeśli połączono, false w przypadku błędu/timeoutu.
 */
void wifi_prov_wait_connected(void);

/**
 * @brief Odczytuje aktualną konfigurację provisioningu z NVS.
 *
 * Brakujące pola zwracane są jako puste stringi (""), a funkcja zwraca ESP_OK
 * jeśli udało się otworzyć namespace w NVS (lub gdy nie istnieje).
 */
esp_err_t wifi_prov_get_config(wifi_prov_config_t *out);

#endif // WIFI_PROV_H
