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
#include "driver/i2c.h"

// --- DOŁĄCZENIE WŁASNEJ BIBLIOTEKI ---
#include "veml7700.h" 

static const char *TAG = "SMART_GARDEN";

// Konfiguracja I2C
#define I2C_MASTER_SCL_IO           22      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0       /*!< I2C master i2c port number */
#define I2C_MASTER_FREQ_HZ          100000  /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0       /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0       /*!< I2C master doesn't need buffer */

// Konfiguracja użytkownika i urządzenia
#define USER_ID "user_jan_banasik"
#define DEVICE_ID "stacja_salon_01"
#define PUBLISH_INTERVAL_MS 10000
#define QUEUE_SIZE 50 

bool water_alert_sent = false;
bool is_mqtt_connected = false;
QueueHandle_t telemetry_queue = NULL;
esp_mqtt_client_handle_t client = NULL;

// Uchwyt do naszego czujnika
veml7700_handle_t veml_sensor;

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

void get_water_level_status(int* water_ok) {
    *water_ok = 1; 
}

// ZMIEIONA FUNKCJA POBIERANIA DANYCH
void get_sensor_data(int *soil_moisture, float *temp, float *humidity, float *pressure, float *light_lux, int* water_ok) {
    // Symulacja innych sensorów (jak wcześniej)
    *soil_moisture = 45 + (rand() % 10);
    *temp = 22.5 + ((float)(rand() % 20) / 10.0);
    *humidity = 40.0 + (rand() % 5);
    *pressure = 1013.0 + (rand() % 2);

    // --- PRAWDZIWY ODCZYT VEML7700 ---
    double lux_val = 0.0;
    
    // Opcjonalnie: automatyczne dopasowanie Gain przed odczytem
    veml7700_auto_adjust_gain(&veml_sensor);
    
    // Odczyt właściwy
    esp_err_t err = veml7700_read_lux(&veml_sensor, &lux_val);
    
    if (err == ESP_OK) {
        *light_lux = (float)lux_val;
        ESP_LOGD(TAG, "VEML7700 Lux: %.2f", lux_val);
    } else {
        ESP_LOGE(TAG, "Błąd odczytu VEML7700!");
        *light_lux = -1.0; // Wartość błędu
    }

    get_water_level_status(water_ok);
}

// Funkcja wysyłająca alert, gdy poziom wody jest niski
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
    // Logika alertu poziomu wody
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
    cJSON_AddNumberToObject(sensors, "soil_moisture_pct", data->soil_moisture);
    cJSON_AddNumberToObject(sensors, "air_temperature_c", data->temp);
    cJSON_AddNumberToObject(sensors, "air_humidity_pct", data->humidity);
    cJSON_AddNumberToObject(sensors, "pressure_hpa", data->pressure);
    cJSON_AddNumberToObject(sensors, "light_lux", data->light_lux);
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
    data.timestamp = esp_log_timestamp(); // Zapisujemy czas pobrania próbki
    
    get_sensor_data(&data.soil_moisture, &data.temp, &data.humidity, 
                    &data.pressure, &data.light_lux, &data.water_ok);

    if (is_mqtt_connected) {
        // Mamy sieć - wysyłamy od razu
        check_water_level(&data.water_ok);
        send_telemetry_json(&data);
    } else {
        // Brak sieci - wrzucamy do kolejki
        if (telemetry_queue != NULL) {
            if (xQueueSend(telemetry_queue, &data, 0) == pdTRUE) {
                ESP_LOGW(TAG, "Brak połączenia! Dane zbuforowane (ts: %lu)", data.timestamp);
            } else {
                ESP_LOGE(TAG, "Bufor pełny! Utracono najnowszy pomiar.");
            }
        }
    }
}

// Task odpowiedzialny za cykliczne wysyłanie danych
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
        
        // Po połączeniu subskrybujemy temat komend - dzięki temu jak user wyślę komendę
        // z aplikacji mobilnej na telefonie, to stacja ją odbierze
        char topic_command[128];
        snprintf(topic_command, sizeof(topic_command), "garden/%s/%s/command", USER_ID, DEVICE_ID);
        esp_mqtt_client_subscribe(client, topic_command, 1);
        ESP_LOGI(TAG, "Zasubskrybowano komendy: %s", topic_command);

        // Opróżnianie bufora z danymi telemetrycznymi po połączeniu
        if (telemetry_queue != NULL) {
            telemetry_data_t buffered_data;
            UBaseType_t items_waiting = uxQueueMessagesWaiting(telemetry_queue);
            
            if (items_waiting > 0) {
                ESP_LOGI(TAG, "Wysyłanie %d zbuforowanych rekordów...", items_waiting);
                while (xQueueReceive(telemetry_queue, &buffered_data, 0) == pdTRUE) {
                    send_telemetry_json(&buffered_data);
                    vTaskDelay(pdMS_TO_TICKS(50)); // Małe opóźnienie, żeby nie zapchać bufora sieciowego
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
        
        // Parsowanie payloadu komendy
        cJSON *cmd_json = cJSON_ParseWithLength(event->data, event->data_len);
        if (cmd_json == NULL) {
            ESP_LOGE(TAG, "Błąd parsowania JSON komendy");
            break;
        }

        cJSON *cmd_item = cJSON_GetObjectItem(cmd_json, "cmd");
        if (cJSON_IsString(cmd_item) && (cmd_item->valuestring != NULL)) {
            
            // Komenda wymuszające pobranie ("odświeżenie") danych z sensorów
            if (strcmp(cmd_item->valuestring, "read_data") == 0) {
                ESP_LOGI(TAG, "Komenda: Wymuszenie odczytu");
                // Wywołujemy funkcję natychmiast
                publish_telemetry_data();
            }
            
            // Komenda włączająca podlewanie na określony czas
            else if (strcmp(cmd_item->valuestring, "water_on") == 0) {
                int duration = 5; // Domyślnie podlewamy 5 sekund
                cJSON *dur_item = cJSON_GetObjectItem(cmd_json, "duration");
                if (cJSON_IsNumber(dur_item)) {
                    duration = dur_item->valueint;
                }
                ESP_LOGI(TAG, "Komenda: Podlewanie przez %d sekund", duration);

                ESP_LOGI(TAG, "START PODLEWANIA (czas: %d s)...", duration);
                
                // Symulacja pracy pompy (blokujemy task na czas trwania)
                // Docelowo tutaj będzie włączane GPIO
                vTaskDelay(pdMS_TO_TICKS(duration * 1000)); 
                
                // Wyłączenie pompy
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
    // Tworzymy kolejkę przed startem MQTT
    telemetry_queue = xQueueCreate(QUEUE_SIZE, sizeof(telemetry_data_t));
    if (telemetry_queue == NULL) {
        ESP_LOGE(TAG, "Nie udało się utworzyć kolejki bufora!");
    }

    // Konfiguracja połączenia
    esp_mqtt_client_config_t mqtt5_cfg = {
        // Adres URL ustawiony jest w menuconfig
        // Format URL: mqtt://użytkownik:hasło@adres_ip:port
        .broker.address.uri = CONFIG_BROKER_URL, 
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,

        // Dane logowania do Mosquitto
        .credentials.username = "admin",  
        .credentials.authentication.password = "admin",
    };

    // Inicjalizacja klienta MQTT do zmiennej zdefiniowanej globalnie
    client = esp_mqtt_client_init(&mqtt5_cfg);

    // Ustawienie obsługi zdarzeń MQTT - gdy cokolwiek stanie się z clientem, wywołane zostanie
    // mqtt5_event_handler
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);

    // Połączenie z brokerem MQTT
    esp_mqtt_client_start(client);
    
    // Uruchomienie zadania wysyłającego dane
    xTaskCreate(publisher_task, "publisher_task", 4096, NULL, 5, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startuje Smart Garden Station...");
    
    // Przygotowanie pamięci trwałej NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // Start stosu sieciowego TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 1. Inicjalizacja magistrali I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C zainicjowane.");

    // 2. Inicjalizacja czujnika VEML7700
    // Używamy portu I2C_MASTER_NUM
    esp_err_t err = veml7700_init(&veml_sensor, I2C_MASTER_NUM);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "VEML7700 znaleziony i skonfigurowany!");
        
        // Opcjonalnie: Ustaw konkretne parametry, np. Gain x2, 100ms
        veml7700_set_config(&veml_sensor, VEML7700_GAIN_2, VEML7700_IT_100MS);
    } else {
        ESP_LOGE(TAG, "Nie wykryto VEML7700 (błąd: %s)", esp_err_to_name(err));
    }

    // Łączenie z WiFi (konfigurowane w menuconfig)
    ESP_ERROR_CHECK(example_connect());

    mqtt5_app_start();
}