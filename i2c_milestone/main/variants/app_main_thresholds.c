#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"

#include "veml7700.h"

// --- VARIANT: Thresholds + obsługa statusów przerwań ALS ---

static const char *TAG = "SMART_GARDEN";

#define I2C_MASTER_SCL_IO           22      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0       /*!< I2C master i2c port number */
#define I2C_MASTER_FREQ_HZ          100000  /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0       /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0       /*!< I2C master doesn't need buffer */

#define USER_ID "user_jan_banasik"
#define DEVICE_ID "stacja_salon_01"
#define PUBLISH_INTERVAL_MS 10000
#define QUEUE_SIZE 50

// VEML7700: progi są w jednostkach RAW (rejestry ALS_WH/ALS_WL).
#define VEML7700_ALS_LOW_THRESHOLD_RAW   100
#define VEML7700_ALS_HIGH_THRESHOLD_RAW  10000

bool water_alert_sent = false;
bool is_mqtt_connected = false;
QueueHandle_t telemetry_queue = NULL;
esp_mqtt_client_handle_t client = NULL;

// Uchwyt do naszego czujnika
veml7700_handle_t veml_sensor;
static SemaphoreHandle_t veml_mutex = NULL;

typedef struct {
    int soil_moisture;
    float temp;
    float humidity;
    float pressure;
    float light_lux;
    int water_ok;
    uint32_t timestamp;
} telemetry_data_t;

// --- INICJALIZACJA I2C ---
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

void send_alert(const char* type, const char* message);

static esp_err_t veml7700_setup_thresholds(veml7700_handle_t *sensor)
{
    esp_err_t err = veml7700_set_interrupts(sensor,
                                           true,
                                           VEML7700_ALS_HIGH_THRESHOLD_RAW,
                                           VEML7700_ALS_LOW_THRESHOLD_RAW);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "VEML7700 thresholds configured (RAW): LOW=%u HIGH=%u",
                 (unsigned)VEML7700_ALS_LOW_THRESHOLD_RAW,
                 (unsigned)VEML7700_ALS_HIGH_THRESHOLD_RAW);
    } else {
        ESP_LOGW(TAG, "VEML7700 thresholds setup failed: %s", esp_err_to_name(err));
    }

    return err;
}

static void veml7700_handle_threshold_status(veml7700_handle_t *sensor)
{
    veml7700_interrupt_status_t status;
    esp_err_t err = veml7700_get_interrupt_status(sensor, &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "VEML7700 interrupt status read failed: %s", esp_err_to_name(err));
        return;
    }

    if (status.was_high_threshold) {
        ESP_LOGW(TAG, "VEML7700: HIGH threshold exceeded");
        send_alert("light", "HIGH_THRESHOLD_EXCEEDED");
    }

    if (status.was_low_threshold) {
        ESP_LOGW(TAG, "VEML7700: LOW threshold exceeded");
        send_alert("light", "LOW_THRESHOLD_EXCEEDED");
    }
}

void get_water_level_status(int* water_ok) {
    *water_ok = 1;
}

void get_sensor_data(int *soil_moisture, float *temp, float *humidity, float *pressure, float *light_lux, int* water_ok) {
    *soil_moisture = 45 + (rand() % 10);
    *temp = 22.5 + ((float)(rand() % 20) / 10.0);
    *humidity = 40.0 + (rand() % 5);
    *pressure = 1013.0 + (rand() % 2);

    double lux_val = 0.0;

    // Blokujemy równoległe requesty/pomiary: VEML7700 + I2C muszą być serializowane.
    if (veml_mutex != NULL) {
        xSemaphoreTake(veml_mutex, portMAX_DELAY);
    }

    // Pojedynczy odczyt: read_lux samo sprawdza RAW, ewentualnie zmienia gain na przyszłość i zwraca lux.
    esp_err_t err = veml7700_read_lux(&veml_sensor, &lux_val);

    // Odczyt statusu progów (polling rejestru ALS_INT) + reakcja.
    veml7700_handle_threshold_status(&veml_sensor);

    if (veml_mutex != NULL) {
        xSemaphoreGive(veml_mutex);
    }

    if (err == ESP_OK) {
        *light_lux = (float)lux_val;
        ESP_LOGI(TAG, "VEML7700 Lux: %.2f", lux_val);
    } else {
        ESP_LOGE(TAG, "Błąd odczytu VEML7700!");
        *light_lux = -1.0;
    }

    get_water_level_status(water_ok);

    ESP_LOGI(TAG, "========== ODCZYT CZUJNIKÓW ==========");
    ESP_LOGI(TAG, "Wilgotność gleby:    %d %%", *soil_moisture);
    ESP_LOGI(TAG, "Temperatura:         %.2f °C", *temp);
    ESP_LOGI(TAG, "Wilgotność powietrza: %.2f %%", *humidity);
    ESP_LOGI(TAG, "Ciśnienie:           %.2f hPa", *pressure);
    ESP_LOGI(TAG, "Natężenie światła:   %.2f lux", *light_lux);
    ESP_LOGI(TAG, "Stan zbiornika:      %s", *water_ok ? "OK" : "NISKI POZIOM");
    ESP_LOGI(TAG, "=======================================");
}

void send_alert(const char* type, const char* message) {
    if (client == NULL) return;

    char topic_alert[128];
    snprintf(topic_alert, sizeof(topic_alert), "garden/%s/%s/alert", USER_ID, DEVICE_ID);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", DEVICE_ID);
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "msg", message);
    cJSON_AddNumberToObject(root, "timestamp", esp_log_timestamp());

    char *json_string = cJSON_PrintUnformatted(root);

    // Alert wysyłamy z QoS 2, aby mieć pewność dostarczenia
    esp_mqtt_client_publish(client, topic_alert, json_string, 0, 2, 0);
    ESP_LOGW(TAG, "Wysłano ALERT: %s", json_string);

    cJSON_Delete(root);
    free(json_string);
}

void check_water_level(int* water_status) {
    // Zakładamy: 1 = OK, 0 = BRAK WODY (Low Level)
    if (*water_status == 0) {
        if (!water_alert_sent) {
            send_alert("water_level", "CRITICAL_LOW");
            water_alert_sent = true;
        }
    } else {
        // Jeśli woda wróciła, resetujemy flagę
        if (water_alert_sent) {
             send_alert("water_level", "NORMAL");
             water_alert_sent = false;
        }
    }
}

void send_telemetry_json(telemetry_data_t *data) {
    if (client == NULL) return;

    char topic_telemetry[128];
    // Budowanie tematu: garden/user_id/device_id/telemetry
    snprintf(topic_telemetry, sizeof(topic_telemetry), "garden/%s/%s/telemetry", USER_ID, DEVICE_ID);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", DEVICE_ID);
    cJSON_AddStringToObject(root, "user", USER_ID);

    // Dodajemy timestamp, żeby serwer wiedział, że to dane historyczne (z bufora)
    cJSON_AddNumberToObject(root, "timestamp", data->timestamp);

    cJSON *sensors = cJSON_CreateObject();
    char temp_str[8], hum_str[8], press_str[8], lux_str[8], soil_str[8];
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
    cJSON_AddBoolToObject(sensors, "water_tank_ok", data->water_ok);

    cJSON_AddItemToObject(root, "sensors", sensors);

    // Konwersja obiektu JSON na zwykły string przed wysłaniem
    char *json_string = cJSON_PrintUnformatted(root);

    // Wysyłanie danych do brokera MQTT
    int msg_id = esp_mqtt_client_publish(client, topic_telemetry, json_string, 0, 1, 0);
    if (msg_id != -1) {
        ESP_LOGI(TAG, "Wysłano dane do tematu: %s", topic_telemetry);
        ESP_LOGI(TAG, "Payload: %s", json_string);
    } else {
        ESP_LOGE(TAG, "Błąd publikacji MQTT");
    }

    // Sprzątanie pamięci
    cJSON_Delete(root);
    free(json_string);
}

void publish_telemetry_data(void) {
    // Zbieramy dane do struktury
    telemetry_data_t data;
    data.timestamp = esp_log_timestamp();

    get_sensor_data(&data.soil_moisture, &data.temp, &data.humidity,
                    &data.pressure, &data.light_lux, &data.water_ok);

    if (is_mqtt_connected) {
        check_water_level(&data.water_ok);
        send_telemetry_json(&data);
    } else {
        if (telemetry_queue != NULL) {
            if (xQueueSend(telemetry_queue, &data, 0) == pdTRUE) {
                ESP_LOGW(TAG, "Brak połączenia! Dane zbuforowane (ts: %lu)", data.timestamp);
            } else {
                ESP_LOGE(TAG, "Bufor pełny! Utracono najnowszy pomiar.");
            }
        }
    }
}

void publisher_task(void *pvParameters) {
    while (1) {
        publish_telemetry_data();
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        is_mqtt_connected = true;

        char topic_command[128];
        snprintf(topic_command, sizeof(topic_command), "garden/%s/%s/command", USER_ID, DEVICE_ID);
        esp_mqtt_client_subscribe(client, topic_command, 1);
        ESP_LOGI(TAG, "Zasubskrybowano komendy: %s", topic_command);

        if (telemetry_queue != NULL) {
            telemetry_data_t buffered_data;
            UBaseType_t items_waiting = uxQueueMessagesWaiting(telemetry_queue);

            if (items_waiting > 0) {
                ESP_LOGI(TAG, "Wysyłanie %d zbuforowanych rekordów...", items_waiting);
                while (xQueueReceive(telemetry_queue, &buffered_data, 0) == pdTRUE) {
                    send_telemetry_json(&buffered_data);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                ESP_LOGI(TAG, "Bufor opróżniony.");
            }
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        is_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Odebrano komendę na temat: %.*s", event->topic_len, event->topic);

        cJSON *cmd_json = cJSON_ParseWithLength(event->data, event->data_len);
        if (cmd_json == NULL) {
            ESP_LOGE(TAG, "Błąd parsowania JSON komendy");
            break;
        }

        cJSON *cmd_item = cJSON_GetObjectItem(cmd_json, "cmd");
        if (cJSON_IsString(cmd_item) && (cmd_item->valuestring != NULL)) {

            if (strcmp(cmd_item->valuestring, "read_data") == 0) {
                ESP_LOGI(TAG, "Komenda: Wymuszenie odczytu");
                publish_telemetry_data();
            }
            else if (strcmp(cmd_item->valuestring, "water_on") == 0) {
                int duration = 5;
                cJSON *dur_item = cJSON_GetObjectItem(cmd_json, "duration");
                if (cJSON_IsNumber(dur_item)) {
                    duration = dur_item->valueint;
                }
                ESP_LOGI(TAG, "Komenda: Podlewanie przez %d sekund", duration);

                ESP_LOGI(TAG, "START PODLEWANIA (czas: %d s)...", duration);
                vTaskDelay(pdMS_TO_TICKS(duration * 1000));
                ESP_LOGI(TAG, "KONIEC PODLEWANIA.");

                send_alert("info", "Watering finished");

                ESP_LOGI(TAG, "Weryfikacja stanu zbiornika po podlaniu...");
                int water_status;
                get_water_level_status(&water_status);
                check_water_level(&water_status);
            }
            else {
                ESP_LOGW(TAG, "Nieznana komenda: %s", cmd_item->valuestring);
            }
        }
        cJSON_Delete(cmd_json);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;

    default:
        break;
    }
}

static void mqtt5_app_start(void)
{
    telemetry_queue = xQueueCreate(QUEUE_SIZE, sizeof(telemetry_data_t));
    if (telemetry_queue == NULL) {
        ESP_LOGE(TAG, "Nie udało się utworzyć kolejki bufora!");
    }

    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,

        .credentials.username = "admin",
        .credentials.authentication.password = "admin",
    };

    client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(client);

    xTaskCreate(publisher_task, "publisher_task", 4096, NULL, 5, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startuje Smart Garden Station (VARIANT: Thresholds)...");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C zainicjowane.");

    veml_mutex = xSemaphoreCreateMutex();
    if (veml_mutex == NULL) {
        ESP_LOGE(TAG, "Nie udało się utworzyć mutexa VEML7700 - równoległe pomiary mogą się przeplatać!");
    }

    esp_err_t err = veml7700_init(&veml_sensor, I2C_MASTER_NUM);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "VEML7700 znaleziony i skonfigurowany!");

        (void)veml7700_set_config(&veml_sensor, VEML7700_GAIN_2, VEML7700_IT_100MS, VEML7700_PERS_1);

        // Konfigurujemy progi + włączamy przerwania ALS.
        (void)veml7700_setup_thresholds(&veml_sensor);
    } else {
        ESP_LOGE(TAG, "Nie wykryto VEML7700 (błąd: %s)", esp_err_to_name(err));
    }

    esp_err_t wifi_err = example_connect();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Nie udało się połączyć z WiFi - kontynuuję bez sieci");
    }

    mqtt5_app_start();
}
