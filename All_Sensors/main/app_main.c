#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
// #include "protocol_examples_common.h" // USUNIĘTE
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common_defs.h"
#include "sensors.h"
#include "mqtt_app.h"
#include "wifi_prov.h" // DODANE

#define TAG "MAIN_APP"
#define PUBLISH_INTERVAL_MS 10000

// Domyślne progi
static sensor_thresholds_t thresholds = {
    .temp_min = 5.0f,
    .hum_min = 20.0f,
    .soil_min = 10,
    .light_min = 100.0f
};

// Flagi stanów alarmowych (zapobiega spamowaniu alertami)
static bool alert_temp_so_far = false;
static bool alert_hum_so_far = false;
static bool alert_soil_so_far = false;
static bool alert_light_so_far = false;
static bool alert_water_so_far = false;

void check_thresholds(telemetry_data_t *data) {
    if (!mqtt_app_is_connected()) return;

    // 1. Temperatura
    if (data->temp < thresholds.temp_min) {
        if (!alert_temp_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Temp %.1f C < min %.1f C", data->temp, thresholds.temp_min);
            mqtt_app_send_alert("temperature_low", msg);
            alert_temp_so_far = true;
        }
    } else {
        alert_temp_so_far = false;
    }

    // 2. Wilgotność
    if (data->humidity < thresholds.hum_min) {
        if (!alert_hum_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Hum %.1f %% < min %.1f %%", data->humidity, thresholds.hum_min);
            mqtt_app_send_alert("humidity_low", msg);
            alert_hum_so_far = true;
        }
    } else {
        alert_hum_so_far = false;
    }

    // 3. Gleba
    if (data->soil_moisture < thresholds.soil_min) {
        if (!alert_soil_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Soil %d %% < min %d %%", data->soil_moisture, thresholds.soil_min);
            mqtt_app_send_alert("soil_moisture_low", msg);
            alert_soil_so_far = true;
        }
    } else {
        alert_soil_so_far = false;
    }

    // 4. Światło
    if (data->light_lux < thresholds.light_min) {
        if (!alert_light_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Light %.1f lux < min %.1f lux", data->light_lux, thresholds.light_min);
            mqtt_app_send_alert("light_low", msg);
            alert_light_so_far = true;
        }
    } else {
        alert_light_so_far = false;
    }

    // 5. Woda
    if (data->water_ok == 1) { // 1 = ALARM
        if (!alert_water_so_far) {
            mqtt_app_send_alert("water_level_critical", "Refill water tank!");
            alert_water_so_far = true;
        }
    } else {
         alert_water_so_far = false;
    }
}

void process_incoming_data(const char *topic, const char *payload, int len) {
    // Sprawdzenie czy to komenda czy progi
    if (strstr(topic, "/command")) {
        ESP_LOGI(TAG, "Odebrano komendę: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
            if (cJSON_IsString(cmd) && (cmd->valuestring != NULL)) {
                
                if (strcmp(cmd->valuestring, "water_on") == 0) {
                    int duration = 5;
                    cJSON *d = cJSON_GetObjectItem(root, "duration");
                    if (cJSON_IsNumber(d)) duration = d->valueint;
                    
                    ESP_LOGI(TAG, "START PODLEWANIA (%d s)", duration);
                    // Tutaj można wywołać kod obsługi pompy, np. włączyć GPIO
                    vTaskDelay(pdMS_TO_TICKS(duration * 1000));
                    ESP_LOGI(TAG, "STOP PODLEWANIA");
                    mqtt_app_send_alert("info", "Watering finished");
                    
                    // Po podlewaniu sprawdź wodę
                    int w_ok;
                    sensors_get_water_status(&w_ok);
                    telemetry_data_t t;
                    t.water_ok = w_ok;
                    // Prosta weryfikacja tylko wody
                    if (t.water_ok == 1) {
                         mqtt_app_send_alert("water_level_critical", "Refill water tank!");
                    }
                } 
                else if (strcmp(cmd->valuestring, "read_data") == 0) {
                    // Wymuszenie odczytu
                    telemetry_data_t data;
                    sensors_read(&data);
                    check_thresholds(&data); // Też sprawdź progi
                    mqtt_app_send_telemetry(&data);
                }
            }
            cJSON_Delete(root);
        }
    } 
    else if (strstr(topic, "/thresholds")) {
        ESP_LOGI(TAG, "Odebrano nowe progi: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            cJSON *item;
            item = cJSON_GetObjectItem(root, "temp_min");
            if (cJSON_IsNumber(item)) thresholds.temp_min = (float)item->valuedouble;
            
            item = cJSON_GetObjectItem(root, "hum_min");
            if (cJSON_IsNumber(item)) thresholds.hum_min = (float)item->valuedouble;

            item = cJSON_GetObjectItem(root, "soil_min");
            if (cJSON_IsNumber(item)) thresholds.soil_min = item->valueint;

            item = cJSON_GetObjectItem(root, "light_min");
            if (cJSON_IsNumber(item)) thresholds.light_min = (float)item->valuedouble;

            ESP_LOGI(TAG, "Zaktualizowano progi: T_min:%.1f, H_min:%.1f, S_min:%d, L_min:%.1f",
                     thresholds.temp_min, thresholds.hum_min, thresholds.soil_min, thresholds.light_min);
            
            cJSON_Delete(root);
        }
    }
}

void publisher_task(void *pvParameters) {
    while (1) {
        telemetry_data_t data;
        
        // 1. Odczyt sensorów
        sensors_read(&data);

        // 2. Weryfikacja progów
        check_thresholds(&data);

        // 3. Wysłanie danych
        mqtt_app_send_telemetry(&data);

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start systemu Smart Garden");
    
    // Inicjalizacja usług systemowych
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Inicjalizacja Provisioningu i WiFi
    wifi_prov_init();
    
    // Inicjalizacja sensorów
    if (sensors_init() != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji sensorów!");
    }

    // Oczekiwanie na połączenie przed startem MQTT (blokujące)
    // Dzięki temu nie startujemy MQTT bez sieci
    ESP_LOGI(TAG, "Oczekiwanie na połączenie WiFi...");
    wifi_prov_wait_connected();
    ESP_LOGI(TAG, "Połączono! Start MQTT.");

    // Start MQTT
    mqtt_app_start(process_incoming_data);

    // Start zadania głównego
    xTaskCreate(publisher_task, "publisher_task", 4096, NULL, 5, NULL);
}
