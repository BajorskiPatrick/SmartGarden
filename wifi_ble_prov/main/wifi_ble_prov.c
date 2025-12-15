#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "driver/gpio.h"

#define LOG_TAG "CUSTOM_PROV"

// --- KONFIGURACJA GPIO ---
// Używamy przycisku BOOT (GPIO 0) do resetu ustawień
#define BUTTON_GPIO GPIO_NUM_0 

// --- UUID dla usługi i charakterystyk (Losowe UUID 128-bit) ---
#define SERVICE_UUID        {0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef} // Przykładowe
// Charakterystyka SSID (Write)
#define CHAR_SSID_UUID      0xFF01 
// Charakterystyka Password (Write)
#define CHAR_PASS_UUID      0xFF02
// Charakterystyka Control (Write - aby zapisać i połączyć)
#define CHAR_CTRL_UUID      0xFF03

// --- Zmienne globalne BLE ---
static uint16_t connection_id = 0;
static bool is_connected = false;
static uint16_t ssid_handle, pass_handle, ctrl_handle;

// Bufory tymczasowe na dane z BLE
static char temp_ssid[32] = {0};
static char temp_pass[64] = {0};

// --- NVS Klucze ---
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

// --- Deklaracje funkcji ---
void start_ble_provisioning();
void stop_ble_provisioning();
void connect_wifi();

// ==========================================================
// OBSŁUGA NVS (Pamięć trwała)
// ==========================================================
esp_err_t save_wifi_credentials(const char* ssid, const char* pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(my_handle, NVS_KEY_PASS, pass);
    
    if (err == ESP_OK) err = nvs_commit(my_handle);
    nvs_close(my_handle);
    return err;
}

esp_err_t load_wifi_credentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(my_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(my_handle, NVS_KEY_PASS, pass, &pass_len);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t clear_wifi_credentials() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    return err;
}

// ==========================================================
// OBSŁUGA WIFI
// ==========================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(LOG_TAG, "WiFi Disconnected. Retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(LOG_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

void connect_wifi(const char* ssid, const char* pass) {
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    
    ESP_LOGI(LOG_TAG, "Connecting to WiFi: SSID=%s", ssid);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

// ==========================================================
// OBSŁUGA BLE (GATT SERVER)
// ==========================================================

#define PROFILE_APP_ID 0

// Konfiguracja reklamowania (Advertising)
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0, // Proste advertising bez UUID w pakiecie, tylko nazwa
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// --- Callbacki GAP (Połączenia / Reklamowanie) ---
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(LOG_TAG, "Advertising start failed");
        } else {
            ESP_LOGI(LOG_TAG, "BLE Advertising started");
        }
        break;
    default:
        break;
    }
}

// --- Callbacki GATTS (Charakterystyki) ---
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        // Po rejestracji aplikacji, konfigurujemy serwis
        esp_ble_gap_set_device_name("ESP32_PROV_CUSTOM");
        esp_ble_gap_config_adv_data(&adv_data);

        // Tworzenie serwisu
        esp_gatt_srvc_id_t service_id;
        service_id.is_primary = true;
        service_id.id.inst_id = 0x00;
        uint8_t svc_uuid128[16] = SERVICE_UUID;
        service_id.id.uuid.len = ESP_UUID_LEN_128;
        memcpy(service_id.id.uuid.uuid.uuid128, svc_uuid128, 16);

        esp_ble_gatts_create_service(gatts_if, &service_id, 10); // Handle num = 10
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        // Dodawanie charakterystyk po utworzeniu serwisu
        uint16_t service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(service_handle);

        // 1. Charakterystyka SSID (Write)
        esp_bt_uuid_t char_ssid_uuid;
        char_ssid_uuid.len = ESP_UUID_LEN_16;
        char_ssid_uuid.uuid.uuid16 = CHAR_SSID_UUID;
        
        esp_ble_gatts_add_char(service_handle, &char_ssid_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // 2. Charakterystyka PASSWORD (Write)
        esp_bt_uuid_t char_pass_uuid;
        char_pass_uuid.len = ESP_UUID_LEN_16;
        char_pass_uuid.uuid.uuid16 = CHAR_PASS_UUID;

        esp_ble_gatts_add_char(service_handle, &char_pass_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // 3. Charakterystyka CONTROL (Write)
        esp_bt_uuid_t char_ctrl_uuid;
        char_ctrl_uuid.len = ESP_UUID_LEN_16;
        char_ctrl_uuid.uuid.uuid16 = CHAR_CTRL_UUID;

        esp_ble_gatts_add_char(service_handle, &char_ctrl_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        // Zapisujemy handle dla każdej dodanej charakterystyki
        // Dodajemy DESKRYPTOR (User Description) dla czytelności w NRF Connect
        uint16_t char_uuid = param->add_char.char_uuid.uuid.uuid16;
        uint16_t attr_handle = param->add_char.attr_handle;
        
        const char *desc_val = NULL;

        if (char_uuid == CHAR_SSID_UUID) {
            ssid_handle = attr_handle;
            desc_val = "WiFi SSID";
        } else if (char_uuid == CHAR_PASS_UUID) {
            pass_handle = attr_handle;
            desc_val = "WiFi Password";
        } else if (char_uuid == CHAR_CTRL_UUID) {
            ctrl_handle = attr_handle;
            desc_val = "Send '1' to Apply";
        }

        if (desc_val != NULL) {
            esp_bt_uuid_t descr_uuid;
            descr_uuid.len = ESP_UUID_LEN_16;
            descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_DESCRIPTION; // 0x2901
            
            esp_ble_gatts_add_char_descr(param->add_char.service_handle, 
                                         &descr_uuid, 
                                         ESP_GATT_PERM_READ,
                                         NULL, NULL);
            // Uwaga: Wartość deskryptora ustawiamy w evencie ADD_CHAR_DESCR_EVT? 
            // W Bluedroid często prościej jest obsłużyć request READ deskryptora, 
            // ale tutaj dodamy go, aby istniał. Wartość ustawimy statycznie lub przez request.
            // Dla uproszczenia tutaj przyjmijmy, że NRF Connect zobaczy UUID, 
            // a zaawansowana obsługa wartości deskryptora wymagałaby ESP_GATTS_ADD_CHAR_DESCR_EVT.
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        // Tutaj można by zainicjować wartość deskryptora, ale NRF Connect często 
        // czyta deskryptor dynamicznie. Zostawmy domyślne.
        break;

    case ESP_GATTS_CONNECT_EVT:
        is_connected = true;
        connection_id = param->connect.conn_id;
        ESP_LOGI(LOG_TAG, "BLE Connected");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        is_connected = false;
        ESP_LOGI(LOG_TAG, "BLE Disconnected");
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_WRITE_EVT: {
        // Obsługa zapisu danych przez telefon
        if (param->write.handle == ssid_handle) {
            memset(temp_ssid, 0, sizeof(temp_ssid));
            memcpy(temp_ssid, param->write.value, param->write.len);
            ESP_LOGI(LOG_TAG, "Received SSID: %s", temp_ssid);
        } 
        else if (param->write.handle == pass_handle) {
            memset(temp_pass, 0, sizeof(temp_pass));
            memcpy(temp_pass, param->write.value, param->write.len);
            ESP_LOGI(LOG_TAG, "Received PASS: %s", temp_pass);
        } 
        else if (param->write.handle == ctrl_handle) {
            // Sprawdź czy użytkownik wysłał '1' (0x31)
            if (param->write.len > 0 && param->write.value[0] == '1') {
                ESP_LOGI(LOG_TAG, "Apply command received. Saving to NVS...");
                save_wifi_credentials(temp_ssid, temp_pass);
                ESP_LOGI(LOG_TAG, "Credentials saved. Rebooting to apply...");
                esp_restart();
            }
        }
        
        // Odpowiedz OK na Write Request (jeśli need_rsp = true)
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    }
    default:
        break;
    }
}

// --- Rejestracja GATTS ---
void start_ble_stack() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gatts_register_callback(gatts_profile_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(PROFILE_APP_ID);
}

// ==========================================================
// MONITOROWANIE PRZYCISKU (Factory Reset / Re-provision)
// ==========================================================
void button_task(void *pvParameter) {
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY); // Button usually connects to GND

    while (1) {
        if (gpio_get_level(BUTTON_GPIO) == 0) { // Pressed
            vTaskDelay(pdMS_TO_TICKS(3000)); // Czekaj 3 sekundy
            if (gpio_get_level(BUTTON_GPIO) == 0) { // Dalej wciśnięty
                ESP_LOGW(LOG_TAG, "Button held! Clearing NVS and restarting...");
                clear_wifi_credentials();
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==========================================================
// MAIN
// ==========================================================
void app_main(void) {
    // 1. Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Start Taska Przyciku
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // 3. Sprawdź czy mamy WiFi w NVS
    char ssid[32] = {0};
    char pass[64] = {0};
    esp_err_t wifi_err = load_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    if (wifi_err == ESP_OK && strlen(ssid) > 0) {
        // Mamy konfigurację - uruchamiamy WiFi
        ESP_LOGI(LOG_TAG, "WiFi credentials found. Connecting to %s...", ssid);
        wifi_init_sta();
        connect_wifi(ssid, pass);
    } else {
        // Brak konfiguracji - uruchamiamy BLE Provisioning
        ESP_LOGI(LOG_TAG, "No WiFi credentials found. Starting BLE Provisioning...");
        start_ble_stack();
    }
}