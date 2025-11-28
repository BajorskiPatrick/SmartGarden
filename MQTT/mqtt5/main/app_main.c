/*
 * Smart Garden MQTT Client
 * Dostosowany do projektu: Inteligentna stacja do monitorowania roślin
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h" // Biblioteka do obsługi JSON

static const char *TAG = "SMART_GARDEN";

// KONFIGURACJA URZĄDZENIA I UŻYTKOWNIKA
#define USER_ID "user_jan_banasik"      // Unikalne ID użytkownika
#define DEVICE_ID "stacja_salon_01"     // Unikalne ID stacji
#define PUBLISH_INTERVAL_MS 5000        // Częstotliwość wysyłania danych

// Zmienne globalne dla klienta MQTT
esp_mqtt_client_handle_t client = NULL;

// Funkcja symulująca odczyt danych z sensorów (zgodnie z prezentacją)
// W finalnej wersji podmienisz te wartości na odczyty z I2C/ADC
void get_sensor_data(int *soil_moisture, float *temp, float *humidity, float *pressure, float *light_lux) {
    // 1. Pojemnościowy czujnik wilgotności gleby (ADC) [cite: 30, 35]
    *soil_moisture = 45 + (rand() % 10); // Symulacja: 45-55%

    // 2. BME280 (I2C) - Temp, Wilgotność powietrza, Ciśnienie [cite: 38, 41]
    *temp = 22.5 + ((float)(rand() % 20) / 10.0);
    *humidity = 40.0 + (rand() % 5);
    *pressure = 1013.0 + (rand() % 2);

    // 3. BH1750 (I2C) - Natężenie światła [cite: 45, 51]
    *light_lux = 1500 + (rand() % 100); 
}

// Zadanie (Task) odpowiedzialne za cykliczne wysyłanie danych
void publisher_task(void *pvParameters) {
    char topic_telemetry[128];
    // Budowanie tematu: garden/user_id/device_id/telemetry
    snprintf(topic_telemetry, sizeof(topic_telemetry), "garden/%s/%s/telemetry", USER_ID, DEVICE_ID);

    while (1) {
        if (client != NULL) {
            int soil;
            float temp, hum, press, lux;
            
            // Pobierz dane
            get_sensor_data(&soil, &temp, &hum, &press, &lux);

            // Tworzenie obiektu JSON
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device", DEVICE_ID);
            cJSON_AddStringToObject(root, "user", USER_ID);
            
            // Sekcja sensorów
            cJSON *sensors = cJSON_CreateObject();
            char temp_str[8], hum_str[8], press_str[8], lux_str[8];
            snprintf(temp_str, sizeof(temp_str), "%.2f",temp);
            snprintf(hum_str, sizeof(hum_str), "%.2f", hum);
            snprintf(press_str, sizeof(press_str), "%.2f", press);
            snprintf(lux_str, sizeof(lux_str), "%.2f", lux);
            cJSON_AddNumberToObject(sensors, "soil_moisture_pct", soil);
            cJSON_AddStringToObject(sensors, "air_temperature_c", temp_str);
            cJSON_AddStringToObject(sensors, "air_humidity_pct", hum_str);
            cJSON_AddStringToObject(sensors, "pressure_hpa", press_str);
            cJSON_AddStringToObject(sensors, "light_lux", lux_str);
            
            cJSON_AddItemToObject(root, "sensors", sensors);

            // Konwersja JSON na string
            char *json_string = cJSON_PrintUnformatted(root);

            // Wysyłanie MQTT
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
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        
        // Tutaj subskrybujemy temat sterowania (np. włączanie podlewania)
        char topic_command[128];
        snprintf(topic_command, sizeof(topic_command), "garden/%s/%s/command", USER_ID, DEVICE_ID);
        esp_mqtt_client_subscribe(client, topic_command, 1);
        ESP_LOGI(TAG, "Zasubskrybowano komendy: %s", topic_command);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Odebrano wiadomość!");
        ESP_LOGI(TAG, "TEMAT=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DANE=%.*s", event->data_len, event->data);
        // Tu dodałbyś logikę obsługi komend (np. włączenie pompy)
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
    // Konfiguracja połączenia
    esp_mqtt_client_config_t mqtt5_cfg = {
        // UWAGA: Adres URL ustawiany jest w menuconfig lub tutaj na sztywno dla testów lokalnych
        // Format: mqtt://użytkownik:hasło@adres_ip:port
        .broker.address.uri = CONFIG_BROKER_URL, 
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,
        // Dane logowania (jeśli Mosquitto je wymaga)
        .credentials.username = "admin",  
        .credentials.authentication.password = "admin",
    };

    client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
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

    // Łączenie z WiFi (konfigurowane w menuconfig)
    ESP_ERROR_CHECK(example_connect());

    mqtt5_app_start();
}