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
#include "qrcode.h"

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char *TAG = "app";

/* Konfiguracja przycisku do resetu (GPIO 0 to zazwyczaj przycisk BOOT na płytkach ESP32) */
#define GPIO_RESET_BUTTON       0
#define RESET_HOLD_TIME_MS      3000

/* Maksymalna liczba prób połączenia podczas provisioningu */
#define MAX_PROV_RETRIES        5

/* Sygnał o poprawnym połączeniu */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

/* Zmienne do kontroli prób połączenia */
static int s_retry_num = 0;
static bool s_is_provisioning = false; // Flaga określająca, czy jesteśmy w trakcie provisioningu

/* Funkcja pomocnicza do rozłączania BLE */
static void disconnect_all_ble_connections(void)
{
    struct ble_gap_conn_desc desc;
    /* NimBLE handles are indices from 0 to MAX_CONNECTIONS-1 */
    for (int i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
        if (ble_gap_conn_find(i, &desc) == 0) {
            ESP_LOGI(TAG, "Wymuszanie rozłączenia BLE, handle: %d", i);
            ble_gap_terminate(i, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
}

/* Funkcja obsługi zdarzeń systemowych */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "--- Rozpoczęto Provisioning (BLE) ---");
                s_is_provisioning = true; // Jesteśmy w trybie provisioningu
                s_retry_num = 0;
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Otrzymano dane WiFi -> SSID: %s", (const char *) wifi_sta_cfg->ssid);
                /* Resetujemy licznik przy każdej nowej próbie podania danych przez użytkownika */
                s_retry_num = 0;
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                /* To zdarzenie może zostać wygenerowane przez managera, jeśli on sam zliczy błędy.
                   Dla pewności również tutaj resetujemy stan. */
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Błąd łączenia z WiFi (Zgłoszone przez Manager)! Powód: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Błąd autoryzacji" : "Nie znaleziono AP");
                
                wifi_prov_mgr_reset_sm_state_on_failure();
                s_retry_num = 0;
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning zakończony sukcesem!");
                s_is_provisioning = false; // Koniec provisioningu
                s_retry_num = 0;
                break;
            case WIFI_PROV_END:
                /* Zwolnienie zasobów managera */
                s_is_provisioning = false;
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        
        /* Czyścimy bit połączenia, aby główna pętla wiedziała, że nie ma WiFi */
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_EVENT);

        /* LOGIKA RESETU PO 5 PRÓBACH */
        if (s_is_provisioning) {
            s_retry_num++;
            ESP_LOGW(TAG, "Nieudana próba połączenia z WiFi podczas provisioningu (%d/%d)", s_retry_num, MAX_PROV_RETRIES);
            
            if (s_retry_num >= MAX_PROV_RETRIES) {
                ESP_LOGE(TAG, "Przekroczono limit prób połączenia. Wracam do trybu rozgłaszania BLE.");
                
                /* To jest kluczowa funkcja, która "cofa" provisioning do momentu oczekiwania na dane,
                   dzięki czemu telefon może ponownie połączyć się z ESP */
                wifi_prov_mgr_reset_sm_state_on_failure();
                
                /* Wymuszamy rozłączenie BLE, aby telefon wiedział, że coś się stało i zwolnił połączenie.
                   Dzięki temu ESP będzie mógł ponownie rozgłaszać się (advertising). */
                disconnect_all_ble_connections();

                s_retry_num = 0; // Reset licznika
                return; // Wychodzimy z funkcji, NIE wywołujemy esp_wifi_connect()
            }
        } else {
            /* Jeśli to normalna praca (już po provisioningu), np. router padł,
               to po prostu próbujemy w nieskończoność. */
            ESP_LOGI(TAG, "Rozłączono z WiFi (Tryb normalny). Ponawiam próbę...");
        }

        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Połączono! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // Sukces, więc zerujemy licznik błędów
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
        s_is_provisioning = true;

        char service_name[12];
        get_device_service_name(service_name, sizeof(service_name));

        /* --- ZMIANA: GENEROWANIE HASŁA (OPCJA A) --- */
        
        /* Pobieramy MAC adres, aby wygenerować unikalne hasło */
        uint8_t eth_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
        
        /* Generujemy hasło: 4 ostatnie bajty MAC (8 znaków HEX). 
           Np. dla MAC AA:BB:CC:11:22:33 hasło to "CC112233" */
        char pop[9]; 
        snprintf(pop, sizeof(pop), "%02X%02X%02X%02X", eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);

        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        /* Używamy wygenerowanego bufora 'pop' zamiast stałego ciągu znaków */
        wifi_prov_security1_params_t *sec_params = (void *)pop;
        const char *service_key = NULL;

        /* Uruchomienie provisioningu */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, service_key));
        
        ESP_LOGI(TAG, "Nazwa urządzenia BLE: %s", service_name);
        ESP_LOGW(TAG, "Proof of Possession (PoP): %s", pop); // Używam LOGW żeby rzuciło się w oczy

        /* --- ZMIANA: GENEROWANIE KODU QR --- */
        
        /* Tworzymy payload w formacie JSON zrozumiałym dla aplikacji ESP Provisioning */
        /* Format: {"ver":"v1","name":"<service_name>","pop":"<pop>","transport":"ble"} */
        char payload[150];
        snprintf(payload, sizeof(payload), 
            "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}",
            service_name, pop);

        ESP_LOGI(TAG, "Zeskanuj poniższy kod QR w aplikacji mobilnej:");
        
        /* Generowanie i wyświetlanie QR w konsoli (ASCII art) */
        esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
        esp_qrcode_generate(&cfg, payload);
        
        /* ----------------------------------- */

    } else {
        ESP_LOGI(TAG, "Urządzenie skonfigurowane. Łączenie z WiFi...");
        s_is_provisioning = false;
        wifi_prov_mgr_deinit();
        wifi_init_sta();
    }

    /* Oczekiwanie na połączenie (blokuje main, ale reszta systemu działa) */
    // xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

    while (1) {
        /* Czekamy na flagę połączenia. 
           pdFALSE - nie czyść flagi po wyjściu (żeby w kolejnej iteracji nie blokowało, jeśli nadal połączone)
           pdFALSE - wait for any bit (mamy tylko jeden)
           portMAX_DELAY - czekaj w nieskończoność
        */
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, pdFALSE, pdFALSE, portMAX_DELAY);

        ESP_LOGI(TAG, "Aplikacja działa... WiFi połączone.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}