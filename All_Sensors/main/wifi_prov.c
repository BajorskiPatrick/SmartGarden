#include "wifi_prov.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_netif.h"
#include "nvs.h" 

// Nagłówki BLE (Bluedroid)
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define LOG_TAG "WIFI_PROV"

// --- KONFIGURACJA GPIO (BOOT Button) ---
#define BUTTON_GPIO GPIO_NUM_0 
#define BUTTON_HOLD_RESET_MS 3000

// --- BLE UUID ---
#define SERVICE_UUID        {0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef}
#define CHAR_SSID_UUID      0xFF01 
#define CHAR_PASS_UUID      0xFF02
#define CHAR_CTRL_UUID      0xFF03

// --- NVS Keys ---
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

// --- Zmienne globalne ---
static EventGroupHandle_t s_wifi_event_group;
// Bit w grupie zdarzeń oznaczający, że mamy połączenie (IP)
#define WIFI_CONNECTED_BIT BIT0

static uint16_t ssid_handle, pass_handle, ctrl_handle;
static bool restart_pending = false;

static char temp_ssid[32] = {0};
static char temp_pass[64] = {0};

static esp_timer_handle_t prov_timeout_timer = NULL;
static bool provisioning_window_open = false;
static bool provisioning_done = false;
static bool ble_stack_started = false;
static volatile bool wifi_credentials_present = false;

#define PROV_ADV_TIMEOUT_MS (2 * 60 * 1000) // 2 minuty na konfigurację

// --- Deklaracje wewn. ---
static void start_provisioning_window(void);
static void stop_ble_provisioning(void);
static void close_provisioning_window(bool provisioning_completed);
static void start_ble_stack(void);
static void connect_wifi(const char* ssid, const char* pass);

// --------------------------------------------------------------------------
// 1. HELPERS (Timer, NVS)
// --------------------------------------------------------------------------

static void restart_timer_cb(void *arg) {
    ESP_LOGI(LOG_TAG, "Restart timer expired. Rebooting system now.");
    esp_restart();
}

static void provisioning_timeout_cb(void *arg) {
    (void)arg;
    if (provisioning_window_open && !provisioning_done) {
        ESP_LOGW(LOG_TAG, "Provisioning timeout (%d ms). Stopping advertising...", PROV_ADV_TIMEOUT_MS);
        close_provisioning_window(false);
    }
}

static esp_err_t save_wifi_credentials(const char* ssid, const char* pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(my_handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(my_handle, NVS_KEY_PASS, pass);
    
    if (err == ESP_OK) err = nvs_commit(my_handle);
    nvs_close(my_handle);

    if (err == ESP_OK) {
        wifi_credentials_present = (ssid != NULL && ssid[0] != '\0');
    }
    return err;
}

// Zwraca ESP_OK jeśli odczytano dane
static esp_err_t load_wifi_credentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len) {
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

static esp_err_t clear_wifi_credentials() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_erase_all(my_handle);
        err = nvs_commit(my_handle);
        nvs_close(my_handle);
        wifi_credentials_present = false;
        ESP_LOGI(LOG_TAG, "NVS cleared.");
    }
    return err;
}


// --------------------------------------------------------------------------
// 2. WiFi Logic
// --------------------------------------------------------------------------

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(LOG_TAG, "WiFi Disconnected. Retrying...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(LOG_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    // Zakładamy, że esp_netif_init() i loop są już utworzone w app_main
    // Ale dla pewności można sprawdzić (helpery IDF są bezpieczne, zwracają błąd jak już jest)
    // Bezpieczniej wywołać create_default_wifi_sta tutaj.
    
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

static void connect_wifi(const char* ssid, const char* pass) {
    wifi_config_t wifi_config = {0};
    strlcpy((char*)wifi_config.sta.ssid, ssid ? ssid : "", sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, pass ? pass : "", sizeof(wifi_config.sta.password));
    
    ESP_LOGI(LOG_TAG, "Connecting to WiFi: SSID=%s", ssid);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

// --------------------------------------------------------------------------
// 3. BLE Logic
// --------------------------------------------------------------------------

#define PROFILE_APP_ID 0

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
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (provisioning_window_open && !provisioning_done) {
            esp_ble_gap_start_advertising(&adv_params);
        }
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

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        esp_ble_gap_set_device_name("SMART_GARDEN_PROV");
        esp_ble_gap_config_adv_data(&adv_data);

        esp_gatt_srvc_id_t service_id;
        service_id.is_primary = true;
        service_id.id.inst_id = 0x00;
        uint8_t svc_uuid128[16] = SERVICE_UUID;
        service_id.id.uuid.len = ESP_UUID_LEN_128;
        memcpy(service_id.id.uuid.uuid.uuid128, svc_uuid128, 16);

        esp_ble_gatts_create_service(gatts_if, &service_id, 10);
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        uint16_t service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(service_handle);

        esp_bt_uuid_t char_uuid;
        char_uuid.len = ESP_UUID_LEN_16;

        // SSID
        char_uuid.uuid.uuid16 = CHAR_SSID_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // PASS
        char_uuid.uuid.uuid16 = CHAR_PASS_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // CTRL
        char_uuid.uuid.uuid16 = CHAR_CTRL_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t uuid = param->add_char.char_uuid.uuid.uuid16;
        if (uuid == CHAR_SSID_UUID) ssid_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_PASS_UUID) pass_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_CTRL_UUID) ctrl_handle = param->add_char.attr_handle;
        break;
    }
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(LOG_TAG, "BLE Client Connected");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(LOG_TAG, "BLE Client Disconnected");
        if (restart_pending) {
            ESP_LOGI(LOG_TAG, "Restart pending. Waiting 1s...");
            esp_timer_create_args_t timer_args = { .callback = &restart_timer_cb, .name = "restart_timer" };
            esp_timer_handle_t r_timer;
            esp_timer_create(&timer_args, &r_timer);
            esp_timer_start_once(r_timer, 1000000);
        } else if (provisioning_window_open && !provisioning_done) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;

    case ESP_GATTS_WRITE_EVT: {
        if (param->write.handle == ssid_handle) {
            memset(temp_ssid, 0, sizeof(temp_ssid));
            size_t len = (param->write.len < sizeof(temp_ssid)-1) ? param->write.len : sizeof(temp_ssid)-1;
            memcpy(temp_ssid, param->write.value, len);
            ESP_LOGI(LOG_TAG, "SSID rcv: %s", temp_ssid);
        } 
        else if (param->write.handle == pass_handle) {
            memset(temp_pass, 0, sizeof(temp_pass));
            size_t len = (param->write.len < sizeof(temp_pass)-1) ? param->write.len : sizeof(temp_pass)-1;
            memcpy(temp_pass, param->write.value, len);
            ESP_LOGI(LOG_TAG, "PASS rcv: ***");
        } 
        else if (param->write.handle == ctrl_handle) {
            if (param->write.len > 0 && param->write.value[0] == '1') {
                ESP_LOGI(LOG_TAG, "Saving credentials & Rebooting...");
                provisioning_done = true;
                close_provisioning_window(true);
                save_wifi_credentials(temp_ssid, temp_pass);
                restart_pending = true;
            }
        }
        
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        if (restart_pending) {
            esp_ble_gatts_close(gatts_if, param->write.conn_id);
        }
        break;
    }
    default:
        break;
    }
}

static void start_ble_stack() {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gatts_register_callback(gatts_profile_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(PROFILE_APP_ID);
}

// --------------------------------------------------------------------------
// 4. Provisioning Control
// --------------------------------------------------------------------------

static void stop_ble_provisioning(void) {
    esp_ble_gap_stop_advertising();
}

static void close_provisioning_window(bool completed) {
    provisioning_window_open = false;
    provisioning_done = completed;
    if (prov_timeout_timer) esp_timer_stop(prov_timeout_timer);
    stop_ble_provisioning();
}

static void start_provisioning_window(void) {
    provisioning_done = false;
    provisioning_window_open = true;

    if (prov_timeout_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = &provisioning_timeout_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "prov_adv_timeout",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&timer_args, &prov_timeout_timer);
    }
    esp_timer_stop(prov_timeout_timer);
    esp_timer_start_once(prov_timeout_timer, (int64_t)PROV_ADV_TIMEOUT_MS * 1000);

    if (!ble_stack_started) {
        ble_stack_started = true;
        start_ble_stack();
    } else {
        esp_ble_gap_start_advertising(&adv_params);
    }
}

// --------------------------------------------------------------------------
// 5. Button Task
// --------------------------------------------------------------------------

static void button_task(void *pvParameter) {
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    while (1) {
        // Active Low
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            int64_t start_us = esp_timer_get_time();
            
            while (gpio_get_level(BUTTON_GPIO) == 0) {
                int64_t ms = (esp_timer_get_time() - start_us) / 1000;
                if (ms >= BUTTON_HOLD_RESET_MS) {
                    ESP_LOGW(LOG_TAG, "Button held > 3s. Clearing NVS & Restart...");
                    clear_wifi_credentials();
                    esp_restart();
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            // Short Click
            int64_t press_ms = (esp_timer_get_time() - start_us) / 1000;
            if (press_ms < BUTTON_HOLD_RESET_MS) {
                if (!wifi_credentials_present) {
                    ESP_LOGI(LOG_TAG, "Click -> Start Config Window.");
                    start_provisioning_window();
                } else {
                    ESP_LOGI(LOG_TAG, "WiFi configured. Ignoring short click.");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500)); // debounce
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// --------------------------------------------------------------------------
// 6. Public API
// --------------------------------------------------------------------------

void wifi_prov_init(void) {
    s_wifi_event_group = xEventGroupCreate();
    
    // Start button task
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // Load Creds
    char ssid[32] = {0};
    char pass[64] = {0};
    esp_err_t err = load_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    wifi_credentials_present = (err == ESP_OK && strlen(ssid) > 0);

    if (wifi_credentials_present) {
        ESP_LOGI(LOG_TAG, "Found stored credentials. Connecting...");
        wifi_init_sta();
        connect_wifi(ssid, pass);
    } else {
        ESP_LOGW(LOG_TAG, "No credentials. Starting Provisioning Mode.");
        start_provisioning_window();
    }
}

void wifi_prov_wait_connected(void) {
    // Czekaj na bit połączenia (bez timeoutu - blokuj do skutku)
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}
