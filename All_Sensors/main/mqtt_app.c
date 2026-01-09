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

static const char *TAG = "MQTT_APP";

#define QUEUE_SIZE 50

static esp_mqtt_client_handle_t client = NULL;
static bool is_connected = false;
static QueueHandle_t telemetry_queue = NULL;
static mqtt_data_callback_t data_callback = NULL;

static char s_user_id[WIFI_PROV_MAX_USER_ID] = {0};
static char s_device_id[13] = {0};
static char s_broker_uri[WIFI_PROV_MAX_BROKER_LEN] = {0};
static char s_mqtt_login[WIFI_PROV_MAX_MQTT_LOGIN] = {0};
static char s_mqtt_pass[WIFI_PROV_MAX_MQTT_PASS] = {0};

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
        
        // 1. Subskrypcja komend
        char topic[256];
        snprintf(topic, sizeof(topic), "garden/%s/%s/command", s_user_id, s_device_id);
        esp_mqtt_client_subscribe(client, topic, 1);
        ESP_LOGI(TAG, "Subskrypcja: %s", topic);

        // 2. Subskrypcja progów (NOWOŚĆ)
        snprintf(topic, sizeof(topic), "garden/%s/%s/thresholds", s_user_id, s_device_id);
        esp_mqtt_client_subscribe(client, topic, 1);
        ESP_LOGI(TAG, "Subskrypcja: %s", topic);

        // 3. Opróżnianie bufora
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
        break;
    
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Odebrano dane na temat: %.*s", event->topic_len, event->topic);
        if (data_callback) {
            // Przekazujemy temat i dane do głównej aplikacji (z terminatorem null dla wygody)
            char *topic_str = strndup(event->topic, event->topic_len);
            char *payload_str = strndup(event->data, event->data_len);
            
            if (topic_str && payload_str) {
                data_callback(topic_str, payload_str, event->data_len);
            }
            
            free(topic_str);
            free(payload_str);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error");
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

void mqtt_app_send_alert(const char* type, const char* message) {
    if (!client) return;
    
    char topic[256];
    snprintf(topic, sizeof(topic), "garden/%s/%s/alert", s_user_id, s_device_id);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", s_device_id);
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "msg", message);
    cJSON_AddNumberToObject(root, "timestamp", esp_log_timestamp());

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, topic, json_str, 0, 2, 0); 
    ESP_LOGW(TAG, "ALERT: %s", json_str);

    cJSON_Delete(root);
    free(json_str);
}

void mqtt_app_send_telemetry(telemetry_data_t *data) {
    // Jeśli brak połączenia, buforujemy
    if (!is_connected) {
        if (telemetry_queue) {
            if (xQueueSend(telemetry_queue, data, 0) == pdTRUE) {
                ESP_LOGW(TAG, "Offline. Zbuforowano dane (ts: %lu)", data->timestamp);
            } else {
                ESP_LOGE(TAG, "Offline. Bufor pełny!");
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
    char temp_str[10], hum_str[10], press_str[10], lux_str[10], soil_str[10];

    snprintf(temp_str, sizeof(temp_str), "%.2f", data->temp);
    snprintf(hum_str, sizeof(hum_str), "%.2f", data->humidity);
    snprintf(press_str, sizeof(press_str), "%.2f", data->pressure);
    snprintf(lux_str, sizeof(lux_str), "%.2f", data->light_lux);
    snprintf(soil_str, sizeof(soil_str), "%d", data->soil_moisture);

    cJSON_AddStringToObject(sensors, "soil_moisture_pct", soil_str);
    cJSON_AddStringToObject(sensors, "air_temperature_c", temp_str);
    cJSON_AddStringToObject(sensors, "air_humidity_pct", hum_str);
    cJSON_AddStringToObject(sensors, "pressure_hpa", press_str);
    cJSON_AddStringToObject(sensors, "light_lux", lux_str);
    cJSON_AddBoolToObject(sensors, "water_tank_ok", (data->water_ok == 0));

    cJSON_AddItemToObject(root, "sensors", sensors);

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, topic, json_str, 0, 1, 0);

    cJSON_Delete(root);
    free(json_str);
}
