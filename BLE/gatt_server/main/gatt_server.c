/*
 * Symulator iTAG (iTAG_simulation) na ESP32 (GATT Server)
 * Zgodny ze specyfikacją ze zrzutów ekranu nRF Connect.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define GATTS_TAG "iTAG_SIM"

/* --- KONFIGURACJA URZĄDZENIA --- */
#define DEVICE_NAME "iTAG_simulation"

/* --- DEFINICJE UUID (Zgodne ze zrzutami ekranu) --- */
#define UUID_SVC_BATTERY      0x180F
#define UUID_CHAR_BATTERY     0x2A19

#define UUID_SVC_ALERT        0x1802
#define UUID_CHAR_ALERT       0x2A06

#define UUID_SVC_BUTTON       0xFFE0
#define UUID_CHAR_BUTTON      0xFFE1

#define PROFILE_NUM      3
#define PROFILE_BATT_ID  0
#define PROFILE_ALERT_ID 1
#define PROFILE_BTN_ID   2

/* --- GLOBALNE UCHWYTY (Handles) dla Taska --- */
static uint16_t g_batt_char_handle = 0;
static uint16_t g_btn_char_handle = 0;
static uint16_t g_conn_id = 0;
static esp_gatt_if_t g_gatt_if = ESP_GATT_IF_NONE;
static bool is_connected = false;

/* Deklaracje handlerów */
static void gatts_profile_batt_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_alert_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void gatts_profile_btn_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

/* Tablica profili */
static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_BATT_ID] = { .gatts_cb = gatts_profile_batt_event_handler, .gatts_if = ESP_GATT_IF_NONE },
    [PROFILE_ALERT_ID] = { .gatts_cb = gatts_profile_alert_event_handler, .gatts_if = ESP_GATT_IF_NONE },
    [PROFILE_BTN_ID] = { .gatts_cb = gatts_profile_btn_event_handler, .gatts_if = ESP_GATT_IF_NONE },
};

/* Parametry Rozgłaszania */
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
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

/* --- TASK SYMULACYJNY (Główna logika czasowa) --- */
void simulation_task(void *pvParameters) {
    uint8_t btn_val = 0x01; // Wartość przycisku (Click)
    uint8_t batt_val = 99;  // Startowy poziom baterii
    uint32_t loop_counter = 0;

    while (1) {
        // Pętla wykonuje się co 5 sekund
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        loop_counter++;

        if (is_connected) {
            // 1. Wyślij przycisk - ZAWSZE co 5 sekund
            if (g_btn_char_handle != 0) {
                ESP_LOGI(GATTS_TAG, "[SIM] Wysyłanie kliknięcia przycisku (FFE1)...");
                esp_ble_gatts_send_indicate(g_gatt_if, g_conn_id, g_btn_char_handle, 1, &btn_val, false);
            }

            // 2. Wyślij baterię - CO DRUGI RAZ (czyli co 10 sekund)
            if (loop_counter % 2 == 0) {
                if (g_batt_char_handle != 0) {
                    ESP_LOGI(GATTS_TAG, "[SIM] Wysyłanie stanu baterii (2A19): %d%%", batt_val);
                    esp_ble_gatts_send_indicate(g_gatt_if, g_conn_id, g_batt_char_handle, 1, &batt_val, false);
                    
                    // Symulacja rozładowywania (opcjonalne)
                    if(batt_val > 0) batt_val--; 
                }
            }
        }
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising start failed");
        } else {
            ESP_LOGI(GATTS_TAG, "Rozgłaszanie jako: %s", DEVICE_NAME);
        }
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            return;
        }
    }

    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

/* =========================================================================
   PROFIL 0: BATTERY SERVICE (0x180F)
   Zgodność ze screenem:
   - UUID: 2A19
   - Properties: Read, Notify
   - Value: 99 (0x63)
   ========================================================================= */
static void gatts_profile_batt_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        gl_profile_tab[PROFILE_BATT_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_BATT_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_BATT_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_BATT_ID].service_id.id.uuid.uuid.uuid16 = UUID_SVC_BATTERY;
        
        esp_ble_gap_set_device_name(DEVICE_NAME);
        esp_ble_gap_config_adv_data(&adv_data);
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_BATT_ID].service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        gl_profile_tab[PROFILE_BATT_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_BATT_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_BATT_ID].char_uuid.uuid.uuid16 = UUID_CHAR_BATTERY;

        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_BATT_ID].service_handle);

        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_BATT_ID].service_handle,
                               &gl_profile_tab[PROFILE_BATT_ID].char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                               NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        gl_profile_tab[PROFILE_BATT_ID].char_handle = param->add_char.attr_handle;
        g_batt_char_handle = param->add_char.attr_handle; // Zapisz dla Taska

        gl_profile_tab[PROFILE_BATT_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_BATT_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_BATT_ID].service_handle,
                                     &gl_profile_tab[PROFILE_BATT_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        break;

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATTS_TAG, "[BATT] Otrzymano żądanie READ");
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 1;
        rsp.attr_value.value[0] = 99; // Stała wartość 99, jak na screenie
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }
    case ESP_GATTS_CONNECT_EVT:
        gl_profile_tab[PROFILE_BATT_ID].conn_id = param->connect.conn_id;
        break;
    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    default: break;
    }
}

/* =========================================================================
   PROFIL 1: IMMEDIATE ALERT (0x1802)
   Zgodność ze screenem:
   - UUID: 2A06
   - Properties: Write, Write No Response, Notify (UWAGA: Notify jest na screenie!)
   - Value: N/A (Write Only logicznie, ale dodajemy Notify bo jest na screenie)
   ========================================================================= */
static void gatts_profile_alert_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        gl_profile_tab[PROFILE_ALERT_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_ALERT_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_ALERT_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_ALERT_ID].service_id.id.uuid.uuid.uuid16 = UUID_SVC_ALERT;
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_ALERT_ID].service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        gl_profile_tab[PROFILE_ALERT_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_ALERT_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_ALERT_ID].char_uuid.uuid.uuid16 = UUID_CHAR_ALERT;
        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_ALERT_ID].service_handle);

        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_ALERT_ID].service_handle,
                               &gl_profile_tab[PROFILE_ALERT_ID].char_uuid,
                               ESP_GATT_PERM_WRITE,
                               /* Zgodnie ze screenem dodajemy też NOTIFY */
                               ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                               NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        // Skoro ma NOTIFY, wypada dodać deskryptor, choć nie będziemy go używać aktywnie
        gl_profile_tab[PROFILE_ALERT_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_ALERT_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_ALERT_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_ALERT_ID].service_handle,
                                     &gl_profile_tab[PROFILE_ALERT_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        break;

    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            
            // --- LOGIKA ALARMU: Reaguj na każdą wartość ---
            ESP_LOGW(GATTS_TAG, "!!! ALARM TRIGGERED [2A06] !!! Received value: 0x%02x", param->write.value[0]);
            
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
        }
        break;
    default: break;
    }
}

/* =========================================================================
   PROFIL 2: BUTTON / UNKNOWN SERVICE (0xFFE0)
   Zgodność ze screenem:
   - UUID: FFE1
   - Properties: Read, Notify
   - Value: 01
   ========================================================================= */
static void gatts_profile_btn_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        gl_profile_tab[PROFILE_BTN_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_BTN_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_BTN_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_BTN_ID].service_id.id.uuid.uuid.uuid16 = UUID_SVC_BUTTON;
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_BTN_ID].service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        gl_profile_tab[PROFILE_BTN_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_BTN_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_BTN_ID].char_uuid.uuid.uuid16 = UUID_CHAR_BUTTON;
        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_BTN_ID].service_handle);

        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_BTN_ID].service_handle,
                               &gl_profile_tab[PROFILE_BTN_ID].char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_READ,
                               NULL, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        gl_profile_tab[PROFILE_BTN_ID].char_handle = param->add_char.attr_handle;
        g_btn_char_handle = param->add_char.attr_handle; // Zapisz dla Taska
        
        gl_profile_tab[PROFILE_BTN_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_BTN_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_BTN_ID].service_handle,
                                     &gl_profile_tab[PROFILE_BTN_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        break;

    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATTS_TAG, "[BTN] Otrzymano żądanie READ");
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 1;
        rsp.attr_value.value[0] = 0x01; // Wartość 01 jak na screenie
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
        g_conn_id = param->connect.conn_id;
        g_gatt_if = gatts_if;
        is_connected = true;
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        is_connected = false;
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_WRITE_EVT:
        if (!param->write.is_prep) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    default: break;
    }
}

void app_main(void)
{
    esp_err_t ret;
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(GATTS_TAG, "BT controller init failed"); return; }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) { ESP_LOGE(GATTS_TAG, "BT controller enable failed"); return; }
    ret = esp_bluedroid_init();
    if (ret) { ESP_LOGE(GATTS_TAG, "Bluedroid init failed"); return; }
    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(GATTS_TAG, "Bluedroid enable failed"); return; }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){ ESP_LOGE(GATTS_TAG, "gatts register error"); return; }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){ ESP_LOGE(GATTS_TAG, "gap register error"); return; }
    
    // Rejestracja 3 profili
    esp_ble_gatts_app_register(PROFILE_BATT_ID);
    esp_ble_gatts_app_register(PROFILE_ALERT_ID);
    esp_ble_gatts_app_register(PROFILE_BTN_ID);

    esp_ble_gatt_set_local_mtu(500);

    // Uruchomienie taska symulacyjnego
    xTaskCreate(simulation_task, "sim_task", 2048, NULL, 10, NULL);
}