#include "mqtt_app.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_mac.h"

#include "wifi_prov.h"
#include "sensors.h"

#include "alert_limiter.h"

#include "alert_limiter.h"

#include <math.h>
#include <sys/time.h>

static const char *TAG = "MQTT_APP";

#define QUEUE_SIZE 50
#define ALERT_QUEUE_SIZE 20

#define PREINIT_ALERTS_MAX 12

static esp_mqtt_client_handle_t client = NULL;
static bool is_connected = false;
static QueueHandle_t telemetry_queue = NULL;
static QueueHandle_t alert_queue = NULL;
static mqtt_data_callback_t data_callback = NULL;

static char s_user_id[WIFI_PROV_MAX_USER_ID] = {0};
static char s_device_id[13] = {0};
static char s_broker_uri[WIFI_PROV_MAX_BROKER_LEN] = {0};
static char s_mqtt_login[WIFI_PROV_MAX_MQTT_LOGIN] = {0};
static char s_mqtt_pass[WIFI_PROV_MAX_MQTT_PASS] = {0};

typedef struct {
    int64_t timestamp_ms;
    char code[48];
    char severity[10];
    char subsystem[16];
    char message[128];
    bool has_details;
    char details_json[256];
} mqtt_alert_record_t;

static mqtt_alert_record_t s_preinit_alerts[PREINIT_ALERTS_MAX];
static size_t s_preinit_alerts_count = 0;

static bool s_telemetry_buffering = false;
static uint32_t s_telemetry_dropped = 0;
static uint32_t s_alert_dropped = 0;

static int64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

static void enqueue_preinit_alert(const mqtt_alert_record_t *rec) {
    if (!rec) return;
    if (s_preinit_alerts_count >= PREINIT_ALERTS_MAX) {
        return;
    }
    s_preinit_alerts[s_preinit_alerts_count++] = *rec;
}

static void publish_alert_record(const mqtt_alert_record_t *rec) {
    if (!client || !rec) return;

    char topic[256];
    snprintf(topic, sizeof(topic), "garden/%s/%s/alert", s_user_id, s_device_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", s_device_id);
    cJSON_AddStringToObject(root, "user", s_user_id);
    cJSON_AddNumberToObject(root, "timestamp", rec->timestamp_ms);

    // Back-compat
    cJSON_AddStringToObject(root, "type", rec->code);
    cJSON_AddStringToObject(root, "msg", rec->message);

    // v2
    cJSON_AddStringToObject(root, "code", rec->code);
    cJSON_AddStringToObject(root, "severity", rec->severity);
    cJSON_AddStringToObject(root, "subsystem", rec->subsystem);
    cJSON_AddStringToObject(root, "message", rec->message);

    if (rec->has_details && rec->details_json[0] != '\0') {
        cJSON *details = cJSON_Parse(rec->details_json);
        if (cJSON_IsObject(details)) {
            cJSON_AddItemToObject(root, "details", details);
        } else {
            if (details) cJSON_Delete(details);
            cJSON_AddNullToObject(root, "details");
        }
    } else {
        cJSON_AddNullToObject(root, "details");
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        esp_mqtt_client_publish(client, topic, json_str, 0, 2, 0);
        free(json_str);
    }

    cJSON_Delete(root);
}

static void send_or_buffer_alert(const mqtt_alert_record_t *rec) {
    if (!rec) return;

    // If MQTT client not started yet, keep a small preinit buffer.
    if (!client) {
        enqueue_preinit_alert(rec);
        return;
    }

    if (is_connected) {
        publish_alert_record(rec);
        return;
    }

    if (alert_queue) {
        if (xQueueSend(alert_queue, rec, 0) != pdTRUE) {
            s_alert_dropped++;
        }
    } else {
        enqueue_preinit_alert(rec);
    }
}

static void mac_to_hex(char *out, size_t out_len) {
    if (!out || out_len < 13) {
        if (out && out_len > 0) out[0] = '\0';
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void mqtt_load_runtime_config(void) {
    wifi_prov_config_t cfg;
    esp_err_t err = wifi_prov_get_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi_prov_get_config failed: %s", esp_err_to_name(err));
        memset(&cfg, 0, sizeof(cfg));
    }

    // DEVICE ID: zawsze MAC (do topiców, device identity)
    mac_to_hex(s_device_id, sizeof(s_device_id));

    // USER ID: tylko z provisioningu
    strlcpy(s_user_id, cfg.user_id, sizeof(s_user_id));

    // BROKER URI: tylko z provisioningu
    strlcpy(s_broker_uri, cfg.broker_uri, sizeof(s_broker_uri));

    // MQTT LOGIN/PASS: tylko z provisioningu
    strlcpy(s_mqtt_login, cfg.mqtt_login, sizeof(s_mqtt_login));
    strlcpy(s_mqtt_pass, cfg.mqtt_pass, sizeof(s_mqtt_pass));

    ESP_LOGI(TAG, "MQTT cfg: broker=%s user_id=%s device_id=%s mqtt_login=%s", s_broker_uri, s_user_id, s_device_id, s_mqtt_login);
}

static bool mqtt_has_required_config(void) {
    return (s_broker_uri[0] != '\0' && s_user_id[0] != '\0' && s_mqtt_login[0] != '\0' && s_mqtt_pass[0] != '\0');
}

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Połączono");
        is_connected = true;

        {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("connection.mqtt_connected", esp_log_timestamp(), 60 * 1000, &suppressed)) {
                char details[96];
                snprintf(details, sizeof(details), "{\"suppressed\":%lu}", (unsigned long)suppressed);
                mqtt_app_send_alert2_details("connection.mqtt_connected", "info", "mqtt", "MQTT connected", details);
            }
        }
        
        // 1. Subskrypcja komend (rozdzielone topici)
        char topic[256];
        snprintf(topic, sizeof(topic), "garden/%s/%s/command/water", s_user_id, s_device_id);
        esp_mqtt_client_subscribe(client, topic, 1);
        ESP_LOGI(TAG, "Subskrypcja: %s", topic);

        snprintf(topic, sizeof(topic), "garden/%s/%s/command/read", s_user_id, s_device_id);
        esp_mqtt_client_subscribe(client, topic, 1);
        ESP_LOGI(TAG, "Subskrypcja: %s", topic);

        // 2. Subskrypcja progów (NOWOŚĆ)
        snprintf(topic, sizeof(topic), "garden/%s/%s/thresholds", s_user_id, s_device_id);
        esp_mqtt_client_subscribe(client, topic, 1);
        ESP_LOGI(TAG, "Subskrypcja: %s", topic);

        // 3. Publikacja capabilities (retained)
        mqtt_app_publish_capabilities();

        // 3b. Flush preinit alerts -> alert_queue
        if (alert_queue && s_preinit_alerts_count > 0) {
            for (size_t i = 0; i < s_preinit_alerts_count; i++) {
                (void)xQueueSend(alert_queue, &s_preinit_alerts[i], 0);
            }
            s_preinit_alerts_count = 0;
        }

        // 3c. Opróżnianie bufora alertów
        if (alert_queue != NULL) {
            mqtt_alert_record_t buffered_alert;
            UBaseType_t alerts_waiting = uxQueueMessagesWaiting(alert_queue);
            if (alerts_waiting > 0) {
                ESP_LOGI(TAG, "Wysyłanie %d zbuforowanych alertów...", alerts_waiting);
                while (xQueueReceive(alert_queue, &buffered_alert, 0) == pdTRUE) {
                    publish_alert_record(&buffered_alert);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }

        // Jeśli w trybie offline zabrakło miejsca na alerty, zgłoś to po odzyskaniu łączności.
        if (s_alert_dropped > 0) {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("alert.buffer_full_dropped", esp_log_timestamp(), 60 * 1000, &suppressed)) {
                char msg[96];
                snprintf(msg, sizeof(msg), "Dropped %lu alerts while offline", (unsigned long)s_alert_dropped);
                char details[160];
                snprintf(details, sizeof(details), "{\"dropped\":%lu,\"queue_size\":%d,\"suppressed\":%lu}",
                         (unsigned long)s_alert_dropped, ALERT_QUEUE_SIZE, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("alert.buffer_full_dropped", "error", "mqtt", msg, details);
            }
            s_alert_dropped = 0;
        }

        // Reset stanu offline telemetry po reconnect
        s_telemetry_buffering = false;

        // 4. Opróżnianie bufora
        if (telemetry_queue != NULL) {
            telemetry_data_t buffered_data;
            UBaseType_t items_waiting = uxQueueMessagesWaiting(telemetry_queue);
            if (items_waiting > 0) {
                ESP_LOGI(TAG, "Wysyłanie %d zbuforowanych rekordów...", items_waiting);
                while (xQueueReceive(telemetry_queue, &buffered_data, 0) == pdTRUE) {
                    mqtt_app_send_telemetry(&buffered_data);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Rozłączono");
        is_connected = false;

        {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("connection.mqtt_disconnected", esp_log_timestamp(), 60 * 1000, &suppressed)) {
                char details[96];
                snprintf(details, sizeof(details), "{\"suppressed\":%lu}", (unsigned long)suppressed);
                mqtt_app_send_alert2_details("connection.mqtt_disconnected", "warning", "mqtt", "MQTT disconnected", details);
            }
        }
        break;
    
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Odebrano dane na temat: %.*s", event->topic_len, event->topic);
        if (data_callback) {
            // Przekazujemy temat i dane do głównej aplikacji (z terminatorem null dla wygody)
            char *topic_str = strndup(event->topic, event->topic_len);
            char *payload_str = strndup(event->data, event->data_len);

            if (topic_str && payload_str) {
                data_callback(topic_str, payload_str, event->data_len);
                free(topic_str);
                free(payload_str);
            } else {
                free(topic_str);
                free(payload_str);

                uint32_t suppressed = 0;
                if (alert_limiter_allow("mqtt.inbound_oom_drop", esp_log_timestamp(), 60 * 1000, &suppressed)) {
                    char details[128];
                    snprintf(details, sizeof(details), "{\"topic_len\":%d,\"payload_len\":%d,\"suppressed\":%lu}",
                             event->topic_len, event->data_len, (unsigned long)suppressed);
                    mqtt_app_send_alert2_details("mqtt.inbound_oom_drop", "error", "mqtt", "Dropped inbound MQTT message (OOM)", details);
                }
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error");

        {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("connection.mqtt_error", esp_log_timestamp(), 60 * 1000, &suppressed)) {
                char details[96];
                snprintf(details, sizeof(details), "{\"suppressed\":%lu}", (unsigned long)suppressed);
                mqtt_app_send_alert2_details("connection.mqtt_error", "error", "mqtt", "MQTT error event", details);
            }
        }
        break;
        
    default:
        break;
    }
}

void mqtt_app_start(mqtt_data_callback_t cb) {
    data_callback = cb;

    mqtt_load_runtime_config();

    if (!mqtt_has_required_config()) {
        ESP_LOGW(TAG, "MQTT config incomplete. Not starting MQTT client.");
        client = NULL;
        is_connected = false;
        return;
    }
    
    telemetry_queue = xQueueCreate(QUEUE_SIZE, sizeof(telemetry_data_t));
    if (telemetry_queue == NULL) {
        ESP_LOGE(TAG, "Błąd tworzenia kolejki!");
    }

    alert_queue = xQueueCreate(ALERT_QUEUE_SIZE, sizeof(mqtt_alert_record_t));
    if (alert_queue == NULL) {
        ESP_LOGE(TAG, "Błąd tworzenia kolejki alertów!");
    }

    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = s_broker_uri,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,
        .credentials = {
            .username = s_mqtt_login,
            .authentication = {
                .password = s_mqtt_pass,
            },
        },
    };

    client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(client);
}

bool mqtt_app_is_connected(void) {
    return is_connected;
}

static double round2(double v) {
    return round(v * 100.0) / 100.0;
}

static void add_number_or_null(cJSON *obj, const char *key, bool include, bool available, double v) {
    if (!include || !available || isnan(v)) {
        cJSON_AddNullToObject(obj, key);
        return;
    }
    cJSON_AddNumberToObject(obj, key, round2(v));
}

static void add_int_or_null(cJSON *obj, const char *key, bool include, bool available, int v) {
    if (!include || !available || v < 0) {
        cJSON_AddNullToObject(obj, key);
        return;
    }
    cJSON_AddNumberToObject(obj, key, v);
}

static void add_bool_or_null(cJSON *obj, const char *key, bool include, bool available, bool v) {
    if (!include || !available) {
        cJSON_AddNullToObject(obj, key);
        return;
    }
    cJSON_AddBoolToObject(obj, key, v);
}

void mqtt_app_send_alert(const char* type, const char* message) {
    // Legacy wrapper: keep old signature but send as v2 as well.
    mqtt_app_send_alert2(type, "warning", "app", message);
}

void mqtt_app_send_alert2(const char* code, const char* severity, const char* subsystem, const char* message) {
    mqtt_app_send_alert2_details(code, severity, subsystem, message, NULL);
}

void mqtt_app_send_alert2_details(const char* code, const char* severity, const char* subsystem, const char* message, const char* details_json) {
    mqtt_alert_record_t rec;
    memset(&rec, 0, sizeof(rec));

    rec.timestamp_ms = get_time_ms();
    strlcpy(rec.code, code ? code : "unknown", sizeof(rec.code));
    strlcpy(rec.severity, severity ? severity : "warning", sizeof(rec.severity));
    strlcpy(rec.subsystem, subsystem ? subsystem : "app", sizeof(rec.subsystem));
    strlcpy(rec.message, message ? message : "", sizeof(rec.message));

    if (details_json && details_json[0] != '\0') {
        rec.has_details = true;
        strlcpy(rec.details_json, details_json, sizeof(rec.details_json));
    }

    send_or_buffer_alert(&rec);
}

void mqtt_app_send_telemetry(telemetry_data_t *data) {
    mqtt_app_send_telemetry_masked(data, TELEMETRY_FIELDS_ALL);
}

void mqtt_app_send_telemetry_masked(telemetry_data_t *data, telemetry_fields_mask_t fields_mask) {
    // Jeśli brak połączenia, buforujemy
    if (!is_connected) {
        if (!s_telemetry_buffering) {
            s_telemetry_buffering = true;
            uint32_t suppressed = 0;
            if (alert_limiter_allow("telemetry.buffering_started", esp_log_timestamp(), 5 * 60 * 1000, &suppressed)) {
                char details[128];
                snprintf(details, sizeof(details), "{\"queue_size\":%d,\"suppressed\":%lu}", QUEUE_SIZE, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("telemetry.buffering_started", "warning", "telemetry", "MQTT offline. Buffering telemetry.", details);
            }
        }

        if (telemetry_queue) {
            if (xQueueSend(telemetry_queue, data, 0) == pdTRUE) {
                ESP_LOGW(TAG, "Offline. Zbuforowano dane (ts: %lu)", data->timestamp);
            } else {
                ESP_LOGE(TAG, "Offline. Bufor pełny!");
                s_telemetry_dropped++;

                uint32_t suppressed = 0;
                if (alert_limiter_allow("telemetry.buffer_full_dropped", esp_log_timestamp(), 60 * 1000, &suppressed)) {
                    char details[160];
                    snprintf(details, sizeof(details), "{\"dropped\":%lu,\"queue_size\":%d,\"suppressed\":%lu}",
                             (unsigned long)s_telemetry_dropped, QUEUE_SIZE, (unsigned long)suppressed);
                    mqtt_app_send_alert2_details("telemetry.buffer_full_dropped", "error", "telemetry", "Telemetry dropped: offline queue full", details);
                    s_telemetry_dropped = 0;
                }
            }
        }
        return;
    }

    // Jeśli jest połączenie, wysyłamy
    char topic[256];
    snprintf(topic, sizeof(topic), "garden/%s/%s/telemetry", s_user_id, s_device_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", s_device_id);
    cJSON_AddStringToObject(root, "user", s_user_id);
    cJSON_AddNumberToObject(root, "timestamp", data->timestamp);

    cJSON *sensors = cJSON_CreateObject();
    telemetry_fields_mask_t available = sensors_get_available_fields_mask();

    bool inc_soil = (fields_mask & TELEMETRY_FIELD_SOIL) != 0;
    bool inc_temp = (fields_mask & TELEMETRY_FIELD_TEMP) != 0;
    bool inc_hum = (fields_mask & TELEMETRY_FIELD_HUM) != 0;
    bool inc_press = (fields_mask & TELEMETRY_FIELD_PRESS) != 0;
    bool inc_light = (fields_mask & TELEMETRY_FIELD_LIGHT) != 0;
    bool inc_water = (fields_mask & TELEMETRY_FIELD_WATER) != 0;

    bool av_soil = (available & TELEMETRY_FIELD_SOIL) != 0;
    bool av_temp = (available & TELEMETRY_FIELD_TEMP) != 0;
    bool av_hum = (available & TELEMETRY_FIELD_HUM) != 0;
    bool av_press = (available & TELEMETRY_FIELD_PRESS) != 0;
    bool av_light = (available & TELEMETRY_FIELD_LIGHT) != 0;
    bool av_water = (available & TELEMETRY_FIELD_WATER) != 0;

    add_int_or_null(sensors, "soil_moisture_pct", inc_soil, av_soil, data->soil_moisture);
    add_number_or_null(sensors, "air_temperature_c", inc_temp, av_temp, data->temp);
    add_number_or_null(sensors, "air_humidity_pct", inc_hum, av_hum, data->humidity);
    add_number_or_null(sensors, "pressure_hpa", inc_press, av_press, data->pressure);
    add_number_or_null(sensors, "light_lux", inc_light, av_light, data->light_lux);
    add_bool_or_null(sensors, "water_tank_ok", inc_water, av_water, (data->water_ok == 0));

    cJSON_AddItemToObject(root, "sensors", sensors);

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, topic, json_str, 0, 1, 0);

    cJSON_Delete(root);
    free(json_str);
}

void mqtt_app_publish_capabilities(void) {
    if (!client) return;
    if (s_user_id[0] == '\0' || s_device_id[0] == '\0') return;

    char topic[256];
    snprintf(topic, sizeof(topic), "garden/%s/%s/capabilities", s_user_id, s_device_id);

    telemetry_fields_mask_t available = sensors_get_available_fields_mask();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", s_device_id);
    cJSON_AddStringToObject(root, "user", s_user_id);
    cJSON_AddNumberToObject(root, "timestamp", get_time_ms());

    cJSON *fields = cJSON_CreateArray();
    if (available & TELEMETRY_FIELD_SOIL) cJSON_AddItemToArray(fields, cJSON_CreateString("soil_moisture_pct"));
    if (available & TELEMETRY_FIELD_TEMP) cJSON_AddItemToArray(fields, cJSON_CreateString("air_temperature_c"));
    if (available & TELEMETRY_FIELD_HUM) cJSON_AddItemToArray(fields, cJSON_CreateString("air_humidity_pct"));
    if (available & TELEMETRY_FIELD_PRESS) cJSON_AddItemToArray(fields, cJSON_CreateString("pressure_hpa"));
    if (available & TELEMETRY_FIELD_LIGHT) cJSON_AddItemToArray(fields, cJSON_CreateString("light_lux"));
    if (available & TELEMETRY_FIELD_WATER) cJSON_AddItemToArray(fields, cJSON_CreateString("water_tank_ok"));
    cJSON_AddItemToObject(root, "fields", fields);

    cJSON *measured = cJSON_CreateObject();
    cJSON_AddBoolToObject(measured, "soil_moisture_pct", (available & TELEMETRY_FIELD_SOIL) != 0);
    cJSON_AddBoolToObject(measured, "air_temperature_c", (available & TELEMETRY_FIELD_TEMP) != 0);
    cJSON_AddBoolToObject(measured, "air_humidity_pct", (available & TELEMETRY_FIELD_HUM) != 0);
    cJSON_AddBoolToObject(measured, "pressure_hpa", (available & TELEMETRY_FIELD_PRESS) != 0);
    cJSON_AddBoolToObject(measured, "light_lux", (available & TELEMETRY_FIELD_LIGHT) != 0);
    cJSON_AddBoolToObject(measured, "water_tank_ok", (available & TELEMETRY_FIELD_WATER) != 0);
    cJSON_AddItemToObject(root, "measured", measured);

    char *json_str = cJSON_PrintUnformatted(root);
    // retained=1 aby backend mógł odczytać stan po subskrypcji
    esp_mqtt_client_publish(client, topic, json_str, 0, 1, 1);

    cJSON_Delete(root);
    free(json_str);
}
