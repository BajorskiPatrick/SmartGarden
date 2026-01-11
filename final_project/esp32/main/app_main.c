#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>

#include <limits.h>

#include "common_defs.h"
#include "sensors.h"
#include "mqtt_app.h"
#include "mqtt_app.h"
#include "wifi_prov.h" // DODANE
#include "esp_sntp.h"

#include "alert_limiter.h"

#define TAG "MAIN_APP"
#define PUBLISH_INTERVAL_MS 10000
#define PUMP_GPIO 2
#define AUTO_WATER_COOLDOWN_MS (30 * 60 * 1000) // 30 minut cooldownu

static int64_t last_water_time = 0;

typedef struct {
    int duration;
    char source[10]; // "auto" or "manual"
} watering_req_t;

static QueueHandle_t watering_req_queue = NULL;


// Domyślne progi - "otwarte" (brak alertów)
// Zmiana nazwy struktury na device_settings_t
typedef struct {
    float temp_min;
    float temp_max;
    float hum_min;
    float hum_max;
    int soil_min;
    int soil_max;
    float light_min;
    float light_max;
    int watering_duration_sec; // NOWE POLA
    int measurement_interval_sec;
} device_settings_t; // Było sensor_thresholds_t

// Domyślne ustawienia
static device_settings_t settings = {
    .temp_min = -INFINITY,
    .temp_max = INFINITY,
    .hum_min = -INFINITY,
    .hum_max = INFINITY,
    .soil_min = INT_MIN,
    .soil_max = INT_MAX,
    .light_min = -INFINITY,
    .light_max = INFINITY,
    .watering_duration_sec = 5, // Domyślnie 5 sekund
    .measurement_interval_sec = 60 // Domyślnie 60 sekund
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

static void save_settings_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    } else {
        err = nvs_set_blob(my_handle, "settings", &settings, sizeof(settings));
        if (err != ESP_OK) {
             ESP_LOGE(TAG, "Failed to save settings to NVS!");
        } else {
             err = nvs_commit(my_handle);
             if (err == ESP_OK) {
                 ESP_LOGI(TAG, "Settings saved to NVS");
             }
        }
        nvs_close(my_handle);
    }
}

static void load_settings_from_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error (%s) opening NVS handle! Using defaults.", esp_err_to_name(err));
    } else {
        size_t required_size = sizeof(settings);
        err = nvs_get_blob(my_handle, "settings", &settings, &required_size);
        if (err != ESP_OK) {
             ESP_LOGW(TAG, "No settings found in NVS. Using defaults.");
        } else {
             ESP_LOGI(TAG, "Settings loaded from NVS");
        }
        nvs_close(my_handle);
    }
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
    if (value_available_float(data->temp) && data->temp < settings.temp_min) {
        if (!alert_temp_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Temp %.1f C < min %.1f C", data->temp, settings.temp_min);
            mqtt_app_send_alert("temperature_low", msg);
            alert_temp_so_far = true;
        }
    } else {
        alert_temp_so_far = false;
    }

    if (value_available_float(data->temp) && data->temp > settings.temp_max) {
        if (!alert_temp_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Temp %.1f C > max %.1f C", data->temp, settings.temp_max);
            mqtt_app_send_alert("temperature_high", msg);
            alert_temp_high_so_far = true;
        }
    } else {
        alert_temp_high_so_far = false;
    }

    // 2. Wilgotność
    if (value_available_float(data->humidity) && data->humidity < settings.hum_min) {
        if (!alert_hum_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Hum %.1f %% < min %.1f %%", data->humidity, settings.hum_min);
            mqtt_app_send_alert("humidity_low", msg);
            alert_hum_so_far = true;
        }
    } else {
        alert_hum_so_far = false;
    }

    if (value_available_float(data->humidity) && data->humidity > settings.hum_max) {
        if (!alert_hum_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Hum %.1f %% > max %.1f %%", data->humidity, settings.hum_max);
            mqtt_app_send_alert("humidity_high", msg);
            alert_hum_high_so_far = true;
        }
    } else {
        alert_hum_high_so_far = false;
    }

    // 3. Gleba
    if (value_available_soil(data->soil_moisture) && data->soil_moisture < settings.soil_min) {
        if (!alert_soil_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Soil %d %% < min %d %%", data->soil_moisture, settings.soil_min);
            mqtt_app_send_alert("soil_moisture_low", msg);
            alert_soil_so_far = true;
        }
    } else {
        alert_soil_so_far = false;
    }

    if (value_available_soil(data->soil_moisture) && data->soil_moisture > settings.soil_max) {
        if (!alert_soil_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Soil %d %% > max %d %%", data->soil_moisture, settings.soil_max);
            mqtt_app_send_alert("soil_moisture_high", msg);
            alert_soil_high_so_far = true;
        }
    } else {
        alert_soil_high_so_far = false;
    }

    // 4. Światło
    if (value_available_float(data->light_lux) && data->light_lux < settings.light_min) {
        if (!alert_light_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Light %.1f lux < min %.1f lux", data->light_lux, settings.light_min);
            mqtt_app_send_alert("light_low", msg);
            alert_light_so_far = true;
        }
    } else {
        alert_light_so_far = false;
    }

    if (value_available_float(data->light_lux) && data->light_lux > settings.light_max) {
        if (!alert_light_high_so_far) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Light %.1f lux > max %.1f lux", data->light_lux, settings.light_max);
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

static void watering_task(void *pvParameters) {
    watering_req_t req;
    while (xQueueReceive(watering_req_queue, &req, portMAX_DELAY) == pdTRUE) {
        char details[64];
        snprintf(details, sizeof(details), "{\"duration\":%d,\"source\":\"%s\"}", req.duration, req.source);
        
        // Alert notify start
        // Uwaga: wysyłanie alertu jest thread-safe (używa kolejki wewnętrznej w mqtt_app)
        if (strcmp(req.source, "auto") == 0) {
            mqtt_app_send_alert2_details("auto_watering_started", "info", "system", "Auto-watering started", details);
        } else {
            mqtt_app_send_alert2("command.watering_started", "info", "command", "Watering started");
        }

        ESP_LOGI(TAG, "START PODLEWANIA (%s, %d s)", req.source, req.duration);
        gpio_set_level(PUMP_GPIO, 1);
        
        // Delay blokujący (teraz blokujemy TYLKO ten task, nie MQTT)
        vTaskDelay(pdMS_TO_TICKS(req.duration * 1000));
        
        gpio_set_level(PUMP_GPIO, 0);
        ESP_LOGI(TAG, "STOP PODLEWANIA");

        // Alert notify stop
        if (strcmp(req.source, "auto") == 0) {
            mqtt_app_send_alert2("auto_watering_finished", "info", "system", "Auto-watering finished");
        } else {
            mqtt_app_send_alert2("command.watering_finished", "info", "command", "Watering finished");
        }

        struct timeval tv;
        gettimeofday(&tv, NULL);
        last_water_time = (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
    }
}

static void perform_watering(int duration, const char* source) {
    if (watering_req_queue) {
        watering_req_t req;
        req.duration = duration;
        strlcpy(req.source, source, sizeof(req.source));
        
        if (xQueueSend(watering_req_queue, &req, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Watering queue full! Ignored request source=%s", source);
        } else {
            ESP_LOGI(TAG, "Watering request queued (source=%s, duration=%d)", source, duration);
        }
    }
}

void process_incoming_data(const char *topic, const char *payload, int len) {
    // Sprawdzenie czy to komenda czy progi
    if (strstr(topic, "/command/water")) {
        ESP_LOGI(TAG, "Odebrano komendę podlewania: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            int duration = settings.watering_duration_sec; // Domyślnie z ustawień
            cJSON *d = cJSON_GetObjectItem(root, "duration");
            if (cJSON_IsNumber(d)) duration = d->valueint;

            int requested = duration;
            const int max_duration_s = 60;
            if (duration < 1) duration = 1;
            if (duration > max_duration_s) duration = max_duration_s;

            if (requested != duration) {
                uint32_t suppressed = 0;
                if (alert_limiter_allow("command.watering_duration_clamped", esp_log_timestamp(), 10 * 1000, &suppressed)) {
                    char details[128];
                    snprintf(details, sizeof(details), "{\"requested\":%d,\"used\":%d,\"suppressed\":%lu}", requested, duration, (unsigned long)suppressed);
                    mqtt_app_send_alert2_details("command.watering_duration_clamped", "warning", "command", "Watering duration clamped", details);
                }
            }

            perform_watering(duration, "manual");

            // Po podlewaniu sprawdź wodę
            int w_ok;
            sensors_get_water_status(&w_ok);
            if (w_ok == 1) {
                mqtt_app_send_alert("water_level_critical", "Refill water tank!");
            }
            cJSON_Delete(root);
        } else {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("command.invalid_json", esp_log_timestamp(), 10 * 1000, &suppressed)) {
                char details[128];
                snprintf(details, sizeof(details), "{\"topic\":\"water\",\"len\":%d,\"suppressed\":%lu}", len, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("command.invalid_json", "warning", "command", "Invalid JSON for command/water", details);
            }
        }
    } 
    else if (strstr(topic, "/command/read")) {
        ESP_LOGI(TAG, "Odebrano komendę odczytu: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (!root) {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("command.invalid_json", esp_log_timestamp(), 10 * 1000, &suppressed)) {
                char details[128];
                snprintf(details, sizeof(details), "{\"topic\":\"read\",\"len\":%d,\"suppressed\":%lu}", len, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("command.invalid_json", "warning", "command", "Invalid JSON for command/read; defaulting to all fields", details);
            }
        }
        telemetry_fields_mask_t mask = parse_fields_mask_from_json(root);

        telemetry_data_t data;
        sensors_read(&data);
        check_thresholds(&data);
        mqtt_app_send_telemetry_masked(&data, mask);

        if (root) cJSON_Delete(root);
    }
    else if (strstr(topic, "/settings")) {
        ESP_LOGI(TAG, "Odebrano nowe ustawienia: %s", payload);
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            device_settings_t new_set = settings;

            bool has_temp_min = false, has_temp_max = false;
            bool has_hum_min = false, has_hum_max = false;
            bool has_soil_min = false, has_soil_max = false;
            bool has_light_min = false, has_light_max = false;

            cJSON *item;

            item = cJSON_GetObjectItem(root, "temp_min");
            if (cJSON_IsNumber(item)) {
                new_set.temp_min = (float)item->valuedouble;
                has_temp_min = true;
            }
            item = cJSON_GetObjectItem(root, "temp_max");
            if (cJSON_IsNumber(item)) {
                new_set.temp_max = (float)item->valuedouble;
                has_temp_max = true;
            }

            item = cJSON_GetObjectItem(root, "hum_min");
            if (cJSON_IsNumber(item)) {
                new_set.hum_min = (float)item->valuedouble;
                has_hum_min = true;
            }
            item = cJSON_GetObjectItem(root, "hum_max");
            if (cJSON_IsNumber(item)) {
                new_set.hum_max = (float)item->valuedouble;
                has_hum_max = true;
            }

            item = cJSON_GetObjectItem(root, "soil_min");
            if (cJSON_IsNumber(item)) {
                new_set.soil_min = item->valueint;
                has_soil_min = true;
            }
            item = cJSON_GetObjectItem(root, "soil_max");
            if (cJSON_IsNumber(item)) {
                new_set.soil_max = item->valueint;
                has_soil_max = true;
            }

            item = cJSON_GetObjectItem(root, "light_min");
            if (cJSON_IsNumber(item)) {
                new_set.light_min = (float)item->valuedouble;
                has_light_min = true;
            }
            item = cJSON_GetObjectItem(root, "light_max");
            if (cJSON_IsNumber(item)) {
                new_set.light_max = (float)item->valuedouble;
                has_light_max = true;
            }

            // Czas podlewania
            item = cJSON_GetObjectItem(root, "watering_duration_sec");
            if (cJSON_IsNumber(item)) {
                new_set.watering_duration_sec = item->valueint;
            }

            // Częstotliwość pomiarów
            item = cJSON_GetObjectItem(root, "measurement_interval_sec");
            if (cJSON_IsNumber(item)) {
                new_set.measurement_interval_sec = item->valueint;
            }

            // Semantyka przedziału: jeśli podano tylko min => max = +inf; jeśli tylko max => min = -inf
            if (has_temp_min && !has_temp_max) new_set.temp_max = INFINITY;
            if (has_temp_max && !has_temp_min) new_set.temp_min = -INFINITY;

            if (has_hum_min && !has_hum_max) new_set.hum_max = INFINITY;
            if (has_hum_max && !has_hum_min) new_set.hum_min = -INFINITY;

            if (has_soil_min && !has_soil_max) new_set.soil_max = INT_MAX;
            if (has_soil_max && !has_soil_min) new_set.soil_min = INT_MIN;

            if (has_light_min && !has_light_max) new_set.light_max = INFINITY;
            if (has_light_max && !has_light_min) new_set.light_min = -INFINITY;

            // Walidacja: min <= max. Jeśli nie, odrzucamy cały update i zostawiamy poprzednie progi.
            bool valid = true;
            if (new_set.temp_min > new_set.temp_max) valid = false;
            if (new_set.hum_min > new_set.hum_max) valid = false;
            if (new_set.soil_min > new_set.soil_max) valid = false;
            if (new_set.light_min > new_set.light_max) valid = false;

            if (new_set.watering_duration_sec < 1) new_set.watering_duration_sec = 1;
            if (new_set.measurement_interval_sec < 5) new_set.measurement_interval_sec = 5; // Min 5 sekund

            if (!valid) {
                ESP_LOGW(TAG,
                         "Odrzucono update ustawień (min > max). Otrzymano: T[%.2f..%.2f], H[%.2f..%.2f], S[%d..%d], L[%.2f..%.2f]",
                         new_set.temp_min, new_set.temp_max,
                         new_set.hum_min, new_set.hum_max,
                         new_set.soil_min, new_set.soil_max,
                         new_set.light_min, new_set.light_max);

                uint32_t suppressed = 0;
                if (alert_limiter_allow("settings.rejected", esp_log_timestamp(), 10 * 1000, &suppressed)) {
                    char details[256];
                    snprintf(details, sizeof(details),
                             "{\"temp\":[%.1f,%.1f],\"hum\":[%.1f,%.1f],\"soil\":[%d,%d],\"light\":[%.1f,%.1f],\"suppressed\":%lu}",
                             new_set.temp_min, new_set.temp_max,
                             new_set.hum_min, new_set.hum_max,
                             new_set.soil_min, new_set.soil_max,
                             new_set.light_min, new_set.light_max,
                             (unsigned long)suppressed);
                    mqtt_app_send_alert2_details("settings.rejected", "warning", "command", "Rejected settings update (min > max)", details);
                }
            } else {
                settings = new_set;
                ESP_LOGI(TAG, "Zaktualizowano ustawienia. Water: %ds, Interval: %ds\n"
                         "    Temp: %.1f .. %.1f\n"
                         "    Hum:  %.1f .. %.1f\n"
                         "    Soil: %d .. %d\n"
                         "    Light: %.1f .. %.1f", 
                         settings.watering_duration_sec, settings.measurement_interval_sec,
                         settings.temp_min, settings.temp_max,
                         settings.hum_min, settings.hum_max,
                         settings.soil_min, settings.soil_max,
                         settings.light_min, settings.light_max);
                save_settings_to_nvs();
            }
            cJSON_Delete(root);
        } else {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("settings.invalid_json", esp_log_timestamp(), 10 * 1000, &suppressed)) {
                char details[96];
                snprintf(details, sizeof(details), "{\"len\":%d,\"suppressed\":%lu}", len, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("settings.invalid_json", "warning", "settings", "Invalid JSON for settings", details);
            }
        }
    }
}

// Handle do taska głównego (do wybudzania po reconnected)
TaskHandle_t publisher_task_handle = NULL;

void publisher_task(void *pvParameters) {
    while (1) {
        telemetry_data_t data;
        
        // 1. Odczyt sensorów
        sensors_read(&data);

        // 2. Weryfikacja progów
        check_thresholds(&data);

        // 3. Wysłanie danych (lub buforowanie jeśli offline)
        // Usunięto sprawdzenie mqtt_app_is_connected(), aby pozwolić na buforowanie wewnątrz funkcji
        mqtt_app_send_telemetry(&data);

        // Autopodlewanie logic
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t now = (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);

        // Uruchom tylko jeśli mamy poprawny odczyt gleby i zdefiniowany próg
        if (data.soil_moisture != -1 && settings.soil_min > -1000) {  // -1000 to bezpieczny margines od -INFINITY/INT_MIN
            if (data.soil_moisture < settings.soil_min) {
                if (now - last_water_time > AUTO_WATER_COOLDOWN_MS) {
                     ESP_LOGW(TAG, "Auto-watering triggered! Soil: %d%% < Min: %d%%", data.soil_moisture, settings.soil_min);
                     perform_watering(settings.watering_duration_sec, "auto"); // Czas z ustawień
                }
            }
        }

        // Oblicz interwał
        int interval_ms = settings.measurement_interval_sec * 1000;
        
        // Adaptacyjny interwał przy braku połączenia (oszczędzanie bufora)
        int buffered_count = mqtt_app_get_consecutive_buffered_count();
        if (buffered_count >= 5) {
             interval_ms = 7200 * 1000; // 2 godziny
             ESP_LOGW(TAG, "Offline mode: 5 consecutive failures. Switching to 2h interval. (Buffered: %d)", buffered_count);
        }

        // Zamiast vTaskDelay, czekamy na notyfikację (np. od MQTT_CONNECTED) LUB timeout
        // Dzięki temu po odzyskaniu połączenia task wybudzi się natychmiast i wróci do normalnego cyklu.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval_ms));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start systemu Smart Garden");

    // Konfiguracja GPIO pompy
    gpio_reset_pin(PUMP_GPIO);
    gpio_set_direction(PUMP_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PUMP_GPIO, 0);

    // Inicjalizacja usług systemowych
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Wczytanie ustawień z NVS
    load_settings_from_nvs();

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
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    
    // NIE BLOKUJEMY na WiFi. Startujemy MQTT i zadania od razu.
    // Jeśli brak sieci, MQTT wejdzie w tryb reconnect, a publisher będzie buforował dane.
    // ESP_LOGI(TAG, "Oczekiwanie na połączenie WiFi...");
    // wifi_prov_wait_connected();
    ESP_LOGI(TAG, "Provisioning kompletny (lub założony). Start MQTT + pomiary niezależnie od statusu WiFi.");

    // Inicjalizacja SNTP
    ESP_LOGI(TAG, "Inicjalizacja SNTP...");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Oczekiwanie na synchronizację czasu (max 30s)
    int retry = 0;
    const int retry_count = 15; // Revert to 30s (15 * 2s)
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Oczekiwanie na czas systemowy... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Aktualny czas: %s", strftime_buf);

    // Start MQTT
    mqtt_app_start(process_incoming_data);

    // Kolejka i task podlewania
    watering_req_queue = xQueueCreate(5, sizeof(watering_req_t));
    xTaskCreate(watering_task, "watering_task", 4096, NULL, 5, NULL);

    // Start zadania głównego (pomiary)
    xTaskCreate(publisher_task, "publisher_task", 4096, NULL, 5, &publisher_task_handle);
}
