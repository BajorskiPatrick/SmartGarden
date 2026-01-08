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
#include "freertos/task.h" // Dodane dla vTaskDelay
#include "i2cdev.h"
#include "bmp280.h"
#include "driver/gpio.h" 
#include "esp_adc/adc_oneshot.h"

// --- DOŁĄCZENIE WŁASNEJ BIBLIOTEKI ---
#include "veml7700.h" 

static const char *TAG = "SMART_GARDEN";

// Konfiguracja I2C
#define I2C_MASTER_SCL_IO           22      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0  /*!< I2C master i2c port number */
#define I2C_MASTER_FREQ_HZ          100000  /*!< I2C master clock frequency */
#define WATER_LEVEL_GPIO            GPIO_NUM_18 /*!< GPIO for water level sensor input */

#define SOIL_ADC_CHANNEL            ADC_CHANNEL_6 
// Wartości kalibracyjne (DO DOSTROJENIA!)
#define SOIL_DRY_VAL                2800            
#define SOIL_WET_VAL                1200            

// Konfiguracja użytkownika i urządzenia
#define USER_ID "user_jan_banasik"
#define DEVICE_ID "stacja_salon_01"
#define PUBLISH_INTERVAL_MS 10000
#define QUEUE_SIZE 50 

bool water_alert_sent = false;
bool is_mqtt_connected = false;
QueueHandle_t telemetry_queue = NULL;
esp_mqtt_client_handle_t client = NULL;

// Uchwyty I2C i czujników
veml7700_handle_t veml_sensor;
bmp280_t bme280_dev;
adc_oneshot_unit_handle_t adc1_handle;

typedef struct {
    int soil_moisture;
    float temp;
    float humidity;
    float pressure;
    float light_lux;
    int water_ok;
    uint32_t timestamp;
} telemetry_data_t;

static void water_sensor_init(void) {
    // Reset pinu (opcjonalne, dla pewności)
    gpio_reset_pin(WATER_LEVEL_GPIO);
    // Ustawienie jako wejście
    gpio_set_direction(WATER_LEVEL_GPIO, GPIO_MODE_INPUT);
    // Włączenie rezystora podciągającego (Pull-up)
    // Dzięki temu, gdy czujnik jest rozwarty (brak wody), stan będzie wysoki (1)
    gpio_set_pull_mode(WATER_LEVEL_GPIO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "Zainicjalizowano czujnik wody na GPIO %d", WATER_LEVEL_GPIO);
}

static void soil_sensor_init(void) {
    // 1. Konfiguracja jednostki ADC (ADC1)
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. Konfiguracja kanału (GPIO 34 / Channel 6)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Zazwyczaj 12 bitów (0-4095)
        .atten = ADC_ATTEN_DB_12,         // 12dB pozwala mierzyć do ok. 3.0V-3.3V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, SOIL_ADC_CHANNEL, &config));

    ESP_LOGI(TAG, "Zainicjalizowano czujnik gleby (ADC Oneshot) na kanale %d", SOIL_ADC_CHANNEL);
}

long map_val(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void get_water_level_status(int* water_ok) {
    // Odczyt stanu pinu (0 lub 1)
    int pin_level = gpio_get_level(WATER_LEVEL_GPIO);

    // LOGIKA ODCZYTU:
    // Pływak podłączony między GPIO 18 a GND.
    // WODA JEST -> Pływak w górze -> Obwód zamknięty do GND -> pin_level = 0
    // BRAK WODY -> Pływak opadł -> Obwód otwarty (Pull-up) -> pin_level = 1
    
    if (pin_level == 0) {
        *water_ok = 0; // Stan OK
    } else {
        *water_ok = 1; // Stan ALARMOWY (brak wody)
    }
}

static esp_err_t bme280_sensor_init(void)
{
    bmp280_params_t params;
    bmp280_init_default_params(&params); 
    
    // [MODYFIKACJA DLA POWER SAVING]
    // Zamiast BMP280_MODE_NORMAL używamy trybu wymuszonego.
    params.mode = BMP280_MODE_FORCED;

    // bmp280_init_desc zainicjuje magistralę I2C wewnątrz biblioteki i2cdev
    esp_err_t err = bmp280_init_desc(&bme280_dev, BMP280_I2C_ADDRESS_0, I2C_MASTER_NUM, 
                                      I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji deskryptora BME280: %s", esp_err_to_name(err));
        return err;
    }

    // Inicjalizacja sensora
    return bmp280_init(&bme280_dev, &params);
}

// ZMIEIONA FUNKCJA POBIERANIA DANYCH
void get_sensor_data(int *soil_moisture, float *temp, float *humidity, float *pressure, float *light_lux, int* water_ok) {
    
    // --- 1. ODCZYT WILGOTNOŚCI GLEBY (Bez zmian) ---
    int raw_adc = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SOIL_ADC_CHANNEL, &raw_adc));
    int percentage = map_val(raw_adc, SOIL_DRY_VAL, SOIL_WET_VAL, 0, 100);
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    
    *soil_moisture = percentage;
    ESP_LOGI(TAG, "[GLEBA] ADC: %d, Wilgotność: %d %%", raw_adc, percentage);
    
    // --- 2. ODCZYT BME280 (Power Saving - Forced Mode) ---
    // Musimy wybudzić czujnik, odczekać na pomiar i odczytać wynik
    esp_err_t err = bmp280_force_measurement(&bme280_dev);
    if (err == ESP_OK) {
        // Czekamy na zakończenie pomiaru (ok. 50ms wystarczy)
        vTaskDelay(pdMS_TO_TICKS(50));

        float bme_temp = 0, bme_press = 0, bme_hum = 0;
        esp_err_t bme_err = bmp280_read_float(&bme280_dev, &bme_temp, &bme_press, &bme_hum);

        if (bme_err == ESP_OK) {
            *temp = bme_temp;
            *pressure = bme_press / 100.0f; 
            *humidity = bme_hum;
            ESP_LOGI(TAG, "[BME280] T: %.2f C, P: %.2f hPa, H: %.2f %%", *temp, *pressure, *humidity);
        } else {
            ESP_LOGE(TAG, "Błąd odczytu BME280: %s", esp_err_to_name(bme_err));
            *temp = 0.0; *pressure = 0.0; *humidity = 0.0;
        }
    } else {
         ESP_LOGE(TAG, "Błąd wybudzania BME280: %s", esp_err_to_name(err));
    }

    // --- 3. ODCZYT VEML7700 (Power Saving jest automatyczny po konfiguracji) ---
    double lux_val = 0.0;
    // Auto adjust gain zadziała poprawnie nawet w trybie PSM (może wymagać dłuższego czasu, ale przy rzadkich odczytach to OK)
    veml7700_auto_adjust_gain(&veml_sensor);
    esp_err_t veml_err = veml7700_read_lux(&veml_sensor, &lux_val);
    
    if (veml_err == ESP_OK) {
        *light_lux = (float)lux_val;
        ESP_LOGI(TAG, "[VEML7700] Lux: %.2f", lux_val);
    } else {
        ESP_LOGE(TAG, "Błąd odczytu VEML7700!");
        *light_lux = -1.0;
    }

    // --- 4. ODCZYT STANU WODY (Standardowy) ---
    get_water_level_status(water_ok);
    ESP_LOGI(TAG, "[WODA] Stan: %s", (*water_ok == 1) ? "OK" : "NISKI POZIOM!");

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
    // UWAGA: Wcześniej logika get_water_level_status zwracała 1 jako ALARM.
    // Ujednolicamy z funkcją get_sensor_data: water_ok=0 (OK), water_ok=1 (Alarm)
    
    if (*water_status == 1) { // Alarm
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
    
    // Logika JSON: water_tank_ok = true (woda jest), false (brak wody)
    // Nasze zmienne: 0=OK, 1=Alarm
    cJSON_AddBoolToObject(sensors, "water_tank_ok", (data->water_ok == 0));

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
        // .credentials.username = "admin",  
        // .credentials.authentication.password = "admin",
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
    
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 1. Inicjalizacja czujników analogowych i cyfrowych
    water_sensor_init();
    soil_sensor_init();
    ESP_ERROR_CHECK(i2cdev_init()); 
    
    // 2. Inicjalizacja czujnika VEML7700 z włączonym Power Saving Mode
    esp_err_t err = veml7700_init_desc(&veml_sensor, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    if (err == ESP_OK) {
        err = veml7700_init(&veml_sensor);
        if (err == ESP_OK) {
            veml7700_set_config(&veml_sensor, VEML7700_GAIN_2, VEML7700_IT_100MS, VEML7700_PERS_1);
            
            // [MODYFIKACJA DLA POWER SAVING] Włączenie PSM Mode 4 (najbardziej oszczędny)
            veml7700_set_power_saving(&veml_sensor, true, VEML7700_PSM_MODE_4);
            ESP_LOGI(TAG, "VEML7700 skonfigurowany (PSM włączone)!");
            
        } else {
             ESP_LOGE(TAG, "VEML7700 błąd init: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "VEML7700 błąd deskryptora: %s", esp_err_to_name(err));
    }

    // 3. Inicjalizacja czujnika BME280 (Tryb Forced jest już ustawiony w bme280_sensor_init)
    if (bme280_sensor_init() == ESP_OK) {
        ESP_LOGI(TAG, "BME280 zainicjowany pomyślnie (Tryb FORCED)!");
    } else {
        ESP_LOGW(TAG, "BME280 problem z inicjalizacją");
    }

    esp_err_t wifi_err = example_connect();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Nie udało się połączyć z WiFi - kontynuuję bez sieci");
    }

    mqtt5_app_start();
}