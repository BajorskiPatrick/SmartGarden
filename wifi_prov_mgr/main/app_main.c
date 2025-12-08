#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

static const char *TAG = "app";

/* Konfiguracja przycisku do resetu (GPIO 0 to zazwyczaj przycisk BOOT na płytkach ESP32) */
#define GPIO_RESET_BUTTON       0
#define RESET_HOLD_TIME_MS      3000

/* Sygnał o poprawnym połączeniu */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

/* Funkcja obsługi zdarzeń systemowych */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "--- Rozpoczęto Provisioning (BLE) ---");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Otrzymano dane WiFi -> SSID: %s", (const char *) wifi_sta_cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Błąd łączenia z WiFi! Powód: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Błąd autoryzacji (złe hasło?)" : "Nie znaleziono AP");
                
                /* KLUCZOWE: Reset maszyny stanów, aby umożliwić ponowną próbę konfiguracji przez BLE bez restartu */
                wifi_prov_mgr_reset_sm_state_on_failure();
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning zakończony sukcesem!");
                break;
            case WIFI_PROV_END:
                /* Zwolnienie zasobów managera */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Rozłączono z WiFi. Ponawiam próbę...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Połączono! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
}

/* Inicjalizacja WiFi w trybie Station */
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Zadanie monitorujące przycisk rekonfiguracji */
void reset_button_task(void *pvParameters)
{
    gpio_set_direction(GPIO_RESET_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_RESET_BUTTON, GPIO_PULLUP_ONLY); // Zakładamy, że przycisk zwiera do GND

    int hold_time = 0;
    while (1) {
        if (gpio_get_level(GPIO_RESET_BUTTON) == 0) { // Przycisk wciśnięty
            hold_time += 100;
            if (hold_time >= RESET_HOLD_TIME_MS) {
                ESP_LOGW(TAG, "Przycisk przytrzymany! Kasowanie ustawień WiFi i restart...");
                
                /* Kasowanie ustawień provisioning z NVS */
                wifi_prov_mgr_reset_provisioning();
                esp_restart();
            }
        } else {
            hold_time = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Generowanie unikalnej nazwy urządzenia BLE na podstawie MAC */
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "PROV_%02X%02X%02X", eth_mac[3], eth_mac[4], eth_mac[5]);
}

void app_main(void)
{
    /* 1. Inicjalizacja NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. Inicjalizacja TCP/IP i Event Loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    /* 3. Rejestracja handlerów zdarzeń */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* 4. Start WiFi i Managera Provisioningu */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Konfiguracja Managera - Tylko BLE */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    /* 5. Uruchomienie Taska od przycisku */
    xTaskCreate(reset_button_task, "reset_btn", 2048, NULL, 10, NULL);

    /* 6. Sprawdzenie czy urządzenie jest już skonfigurowane */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(TAG, "Urządzenie nieskonfigurowane. Uruchamianie BLE...");

        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        /* Security 1 (Proof of Possession) - Proste i bezpieczne */
        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        const char *pop = "Haslo123"; // Kod PoP, który trzeba wpisać w aplikacji
        wifi_prov_security1_params_t *sec_params = (void *)pop;
        const char *service_key = NULL; // Nieużywane przy BLE

        /* Uruchomienie provisioningu */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, service_key));
        
        ESP_LOGI(TAG, "Nazwa urządzenia BLE: %s", service_name);
        ESP_LOGI(TAG, "Proof of Possession (PoP): %s", pop);
    } else {
        ESP_LOGI(TAG, "Urządzenie skonfigurowane. Łączenie z WiFi...");
        wifi_prov_mgr_deinit(); // Nie potrzebujemy managera, jeśli już mamy config
        wifi_init_sta();
    }

    /* Oczekiwanie na połączenie (blokuje main, ale reszta systemu działa) */
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

    while (1) {
        ESP_LOGI(TAG, "Aplikacja działa... WiFi połączone.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}