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

#include <math.h>

#include <limits.h>

#include "common_defs.h"
#include "sensors.h"
#include "mqtt_app.h"
#include "wifi_prov.h" // DODANE

#define TAG "MAIN_APP"
#define PUBLISH_INTERVAL_MS 10000

// Domyślne progi
static sensor_thresholds_t thresholds = {
    .temp_min = 5.0f,
    .temp_max = INFINITY,
    .hum_min = 20.0f,
    .hum_max = INFINITY,
    .soil_min = 10,
    .soil_max = INT_MAX,
    .light_min = 100.0f,
    .light_max = INFINITY
};

// Flagi stanów alarmowych (zapobiega spamowaniu alertami)
static bool alert_temp_so_far = false;
static bool alert_temp_high_so_far = false;
static bool alert_hum_so_far = false;
static bool alert_hum_high_so_far = false;
static bool alert_soil_so_far = false;
static bool alert_soil_high_so_far = false;
static bool alert_light_so_far = false;
static bool alert_light_high_so_far = false;
static bool alert_water_so_far = false;

static bool value_available_float(float v) {
    return !isnan(v);
}

static bool value_available_soil(int v) {
    return v >= 0;
}

static telemetry_fields_mask_t parse_fields_mask_from_json(cJSON *root) {
    telemetry_fields_mask_t mask = 0;
    if (!root) return TELEMETRY_FIELDS_ALL;

    // Wspieramy: {"field":"air_temperature_c"} lub {"fields":["air_temperature_c", ...]}
    cJSON *field = cJSON_GetObjectItem(root, "field");
    if (cJSON_IsString(field) && field->valuestring) {
        const char *s = field->valuestring;
        if (strcmp(s, "soil_moisture_pct") == 0) mask |= TELEMETRY_FIELD_SOIL;
        else if (strcmp(s, "air_temperature_c") == 0) mask |= TELEMETRY_FIELD_TEMP;
        else if (strcmp(s, "air_humidity_pct") == 0) mask |= TELEMETRY_FIELD_HUM;
        else if (strcmp(s, "pressure_hpa") == 0) mask |= TELEMETRY_FIELD_PRESS;
        else if (strcmp(s, "light_lux") == 0) mask |= TELEMETRY_FIELD_LIGHT;
        else if (strcmp(s, "water_tank_ok") == 0) mask |= TELEMETRY_FIELD_WATER;
    }

    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    if (cJSON_IsArray(fields)) {
        int n = cJSON_GetArraySize(fields);
        for (int i = 0; i < n; i++) {
            cJSON *it = cJSON_GetArrayItem(fields, i);
            if (!cJSON_IsString(it) || !it->valuestring) continue;
            const char *s = it->valuestring;
            if (strcmp(s, "soil_moisture_pct") == 0) mask |= TELEMETRY_FIELD_SOIL;
            else if (strcmp(s, "air_temperature_c") == 0) mask |= TELEMETRY_FIELD_TEMP;
            else if (strcmp(s, "air_humidity_pct") == 0) mask |= TELEMETRY_FIELD_HUM;
            else if (strcmp(s, "pressure_hpa") == 0) mask |= TELEMETRY_FIELD_PRESS;
            else if (strcmp(s, "light_lux") == 0) mask |= TELEMETRY_FIELD_LIGHT;
            else if (strcmp(s, "water_tank_ok") == 0) mask |= TELEMETRY_FIELD_WATER;
        }
    }

    // Jeśli nie podano żadnych pól, traktujemy jako "wszystko"
    if (mask == 0) return TELEMETRY_FIELDS_ALL;
    return mask;
}

void check_thresholds(telemetry_data_t *data) {
    if (!mqtt_app_is_connected()) return;

    // 1. Temperatura
    if (value_available_float(data->temp) && data->temp < thresholds.temp_min) {
        if (!alert_temp_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Temp %.1f C < min %.1f C", data->temp, thresholds.temp_min);
            mqtt_app_send_alert("temperature_low", msg);
            alert_temp_so_far = true;
        }
    } else {
        alert_temp_so_far = false;
    }

    if (value_available_float(data->temp) && data->temp > thresholds.temp_max) {
        if (!alert_temp_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Temp %.1f C > max %.1f C", data->temp, thresholds.temp_max);
            mqtt_app_send_alert("temperature_high", msg);
            alert_temp_high_so_far = true;
        }
    } else {
        alert_temp_high_so_far = false;
    }

    // 2. Wilgotność
    if (value_available_float(data->humidity) && data->humidity < thresholds.hum_min) {
        if (!alert_hum_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Hum %.1f %% < min %.1f %%", data->humidity, thresholds.hum_min);
            mqtt_app_send_alert("humidity_low", msg);
            alert_hum_so_far = true;
        }
    } else {
        alert_hum_so_far = false;
    }

    if (value_available_float(data->humidity) && data->humidity > thresholds.hum_max) {
        if (!alert_hum_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Hum %.1f %% > max %.1f %%", data->humidity, thresholds.hum_max);
            mqtt_app_send_alert("humidity_high", msg);
            alert_hum_high_so_far = true;
        }
    } else {
        alert_hum_high_so_far = false;
    }

    // 3. Gleba
    if (value_available_soil(data->soil_moisture) && data->soil_moisture < thresholds.soil_min) {
        if (!alert_soil_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Soil %d %% < min %d %%", data->soil_moisture, thresholds.soil_min);
            mqtt_app_send_alert("soil_moisture_low", msg);
            alert_soil_so_far = true;
        }
    } else {
        alert_soil_so_far = false;
    }

    if (value_available_soil(data->soil_moisture) && data->soil_moisture > thresholds.soil_max) {
        if (!alert_soil_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Soil %d %% > max %d %%", data->soil_moisture, thresholds.soil_max);
            mqtt_app_send_alert("soil_moisture_high", msg);
            alert_soil_high_so_far = true;
        }
    } else {
        alert_soil_high_so_far = false;
    }

    // 4. Światło
    if (value_available_float(data->light_lux) && data->light_lux < thresholds.light_min) {
        if (!alert_light_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Light %.1f lux < min %.1f lux", data->light_lux, thresholds.light_min);
            mqtt_app_send_alert("light_low", msg);
            alert_light_so_far = true;
        }
    } else {
        alert_light_so_far = false;
    }

    if (value_available_float(data->light_lux) && data->light_lux > thresholds.light_max) {
        if (!alert_light_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Light %.1f lux > max %.1f lux", data->light_lux, thresholds.light_max);
            mqtt_app_send_alert("light_high", msg);
            alert_light_high_so_far = true;
        }
    } else {
        alert_light_high_so_far = false;
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
    if (strstr(topic, "/command/water")) {
        ESP_LOGI(TAG, "Odebrano komendę podlewania: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            int duration = 5;
            cJSON *d = cJSON_GetObjectItem(root, "duration");
            if (cJSON_IsNumber(d)) duration = d->valueint;

            ESP_LOGI(TAG, "START PODLEWANIA (%d s)", duration);
            // TODO: sterowanie pompą GPIO
            vTaskDelay(pdMS_TO_TICKS(duration * 1000));
            ESP_LOGI(TAG, "STOP PODLEWANIA");
            mqtt_app_send_alert("info", "Watering finished");

            // Po podlewaniu sprawdź wodę
            int w_ok;
            sensors_get_water_status(&w_ok);
            if (w_ok == 1) {
                mqtt_app_send_alert("water_level_critical", "Refill water tank!");
            }
            cJSON_Delete(root);
        }
    } 
    else if (strstr(topic, "/command/read")) {
        ESP_LOGI(TAG, "Odebrano komendę odczytu: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        telemetry_fields_mask_t mask = parse_fields_mask_from_json(root);

        telemetry_data_t data;
        sensors_read(&data);
        check_thresholds(&data);
        mqtt_app_send_telemetry_masked(&data, mask);

        if (root) cJSON_Delete(root);
    }
    else if (strstr(topic, "/thresholds")) {
        ESP_LOGI(TAG, "Odebrano nowe progi: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            sensor_thresholds_t new_thr = thresholds;

            bool has_temp_min = false, has_temp_max = false;
            bool has_hum_min = false, has_hum_max = false;
            bool has_soil_min = false, has_soil_max = false;
            bool has_light_min = false, has_light_max = false;

            cJSON *item;

            item = cJSON_GetObjectItem(root, "temp_min");
            if (cJSON_IsNumber(item)) {
                new_thr.temp_min = (float)item->valuedouble;
                has_temp_min = true;
            }
            item = cJSON_GetObjectItem(root, "temp_max");
            if (cJSON_IsNumber(item)) {
                new_thr.temp_max = (float)item->valuedouble;
                has_temp_max = true;
            }

            item = cJSON_GetObjectItem(root, "hum_min");
            if (cJSON_IsNumber(item)) {
                new_thr.hum_min = (float)item->valuedouble;
                has_hum_min = true;
            }
            item = cJSON_GetObjectItem(root, "hum_max");
            if (cJSON_IsNumber(item)) {
                new_thr.hum_max = (float)item->valuedouble;
                has_hum_max = true;
            }

            item = cJSON_GetObjectItem(root, "soil_min");
            if (cJSON_IsNumber(item)) {
                new_thr.soil_min = item->valueint;
                has_soil_min = true;
            }
            item = cJSON_GetObjectItem(root, "soil_max");
            if (cJSON_IsNumber(item)) {
                new_thr.soil_max = item->valueint;
                has_soil_max = true;
            }

            item = cJSON_GetObjectItem(root, "light_min");
            if (cJSON_IsNumber(item)) {
                new_thr.light_min = (float)item->valuedouble;
                has_light_min = true;
            }
            item = cJSON_GetObjectItem(root, "light_max");
            if (cJSON_IsNumber(item)) {
                new_thr.light_max = (float)item->valuedouble;
                has_light_max = true;
            }

            // Semantyka przedziału: jeśli podano tylko min => max = +inf; jeśli tylko max => min = -inf
            if (has_temp_min && !has_temp_max) new_thr.temp_max = INFINITY;
            if (has_temp_max && !has_temp_min) new_thr.temp_min = -INFINITY;

            if (has_hum_min && !has_hum_max) new_thr.hum_max = INFINITY;
            if (has_hum_max && !has_hum_min) new_thr.hum_min = -INFINITY;

            if (has_soil_min && !has_soil_max) new_thr.soil_max = INT_MAX;
            if (has_soil_max && !has_soil_min) new_thr.soil_min = INT_MIN;

            if (has_light_min && !has_light_max) new_thr.light_max = INFINITY;
            if (has_light_max && !has_light_min) new_thr.light_min = -INFINITY;

            // Walidacja: min <= max. Jeśli nie, odrzucamy cały update i zostawiamy poprzednie progi.
            bool valid = true;
            if (new_thr.temp_min > new_thr.temp_max) valid = false;
            if (new_thr.hum_min > new_thr.hum_max) valid = false;
            if (new_thr.soil_min > new_thr.soil_max) valid = false;
            if (new_thr.light_min > new_thr.light_max) valid = false;

            if (!valid) {
                ESP_LOGW(TAG,
                         "Odrzucono update progów (min > max). Otrzymano: T[%.2f..%.2f], H[%.2f..%.2f], S[%d..%d], L[%.2f..%.2f]",
                         new_thr.temp_min, new_thr.temp_max,
                         new_thr.hum_min, new_thr.hum_max,
                         new_thr.soil_min, new_thr.soil_max,
                         new_thr.light_min, new_thr.light_max);
                cJSON_Delete(root);
                return;
            }

            thresholds = new_thr;

            ESP_LOGI(TAG,
                     "Zaktualizowano progi: T[%.2f..%.2f], H[%.2f..%.2f], S[%d..%d], L[%.2f..%.2f]",
                     thresholds.temp_min, thresholds.temp_max,
                     thresholds.hum_min, thresholds.hum_max,
                     thresholds.soil_min, thresholds.soil_max,
                     thresholds.light_min, thresholds.light_max);
            
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

    // Inicjalizacja sensorów (Wcześniej niż WiFi/BLE, żeby uniknąć zakłóceń przy starcie I2C)
    if (sensors_init() != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji sensorów!");
    }

    // Inicjalizacja Provisioningu i WiFi
    wifi_prov_init();

    // Jeśli brakuje danych do MQTT/WiFi, wstrzymujemy działanie i prosimy o provisioning.
    // (W przeciwnym razie moglibyśmy zablokować się na wifi_prov_wait_connected gdy SSID nie jest jeszcze ustawione.)
    while (!wifi_prov_is_fully_provisioned()) {
        if (!wifi_prov_is_provisioning_active()) {
            ESP_LOGW(TAG, "Brak pełnego provisioningu (SSID+broker+mqtt login/pass+user_id). Pomiary wstrzymane. Uruchom provisioning przyciskiem.");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Oczekiwanie na połączenie przed startem MQTT (blokujące)
    // Dzięki temu nie startujemy MQTT bez sieci
    ESP_LOGI(TAG, "Oczekiwanie na połączenie WiFi...");
    wifi_prov_wait_connected();
    ESP_LOGI(TAG, "Połączono i provisioning kompletny. Start MQTT + pomiary.");

    // Start MQTT
    mqtt_app_start(process_incoming_data);

    // Start zadania głównego (pomiary)
    xTaskCreate(publisher_task, "publisher_task", 4096, NULL, 5, NULL);
}
