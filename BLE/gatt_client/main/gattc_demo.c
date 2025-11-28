#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define GATTC_TAG "GATTC_DEMO"

/* ZMIANA 1: Ustawiamy UUID standardowego serwisu baterii */
#define REMOTE_SERVICE_UUID        0x180F

/* ZMIANA 2: Ustawiamy UUID charakterystyki poziomu baterii */
#define REMOTE_NOTIFY_CHAR_UUID    0x2A19

#define GATTC_TAG "GATTC_DEMO"

/* --- DEFINICJE UUID --- */
// 1. Battery Service
#define UUID_SVC_BATTERY      0x180F
#define UUID_CHAR_BATTERY     0x2A19

// 2. Immediate Alert Service
#define UUID_SVC_ALERT        0x1802
#define UUID_CHAR_ALERT       0x2A06

// 3. Unknown Service (Przyciski / Custom)
#define UUID_SVC_UNKNOWN      0xFFE0
#define UUID_CHAR_UNKNOWN     0xFFE1

#define PROFILE_NUM      1
#define PROFILE_A_APP_ID 0
#define INVALID_HANDLE   0
#if CONFIG_EXAMPLE_INIT_DEINIT_LOOP
#define EXAMPLE_TEST_COUNT 50
#endif

// Zmienne do przechowywania uchwytów (handles) dla znalezionych serwisów
static uint16_t h_batt_start = 0, h_batt_end = 0;
static uint16_t h_alert_start = 0, h_alert_end = 0;
static uint16_t h_unk_start = 0,  h_unk_end = 0;

// Zmienne do przechowywania uchwytów charakterystyk (żeby wiedzieć, co przyszło)
static uint16_t h_char_batt = INVALID_HANDLE;
static uint16_t h_char_alert = INVALID_HANDLE;
static uint16_t h_char_unk  = INVALID_HANDLE;

static bool connect    = false;

static uint8_t click_count = 0;        // Licznik kliknięć
static bool trigger_alarm = false;     // Flaga "uruchom alarm" dla pętli głównej

/* Declare static functions */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "GATT client register, status %d", param->reg.status);
        esp_ble_gap_set_scan_params(&ble_scan_params);
        break;

    case ESP_GATTC_CONNECT_EVT:{
        ESP_LOGI(GATTC_TAG, "Connected, conn_id %d", p_data->connect.conn_id);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->connect.conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
        esp_ble_gattc_send_mtu_req (gattc_if, p_data->connect.conn_id);
        break;
    }

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Open failed, status %d", p_data->open.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Open success, MTU %u", p_data->open.mtu);
        break;

    case ESP_GATTC_CFG_MTU_EVT:
        ESP_LOGI(GATTC_TAG, "MTU exchange, status %d", param->cfg_mtu.status);
        // Po wymianie MTU startujemy szukanie serwisów. NULL = szukaj wszystkich.
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        // Sprawdzamy UUID znalezionego serwisu i zapisujemy jego uchwyty
        esp_gatt_id_t *srvc_id = &p_data->search_res.srvc_id;
        if (srvc_id->uuid.len == ESP_UUID_LEN_16) {
            if (srvc_id->uuid.uuid.uuid16 == UUID_SVC_BATTERY) {
                ESP_LOGI(GATTC_TAG, "Found BATTERY Service (180F)");
                h_batt_start = p_data->search_res.start_handle;
                h_batt_end   = p_data->search_res.end_handle;
            } else if (srvc_id->uuid.uuid.uuid16 == UUID_SVC_ALERT) {
                ESP_LOGI(GATTC_TAG, "Found ALERT Service (1802)");
                h_alert_start = p_data->search_res.start_handle;
                h_alert_end   = p_data->search_res.end_handle;
            } else if (srvc_id->uuid.uuid.uuid16 == UUID_SVC_UNKNOWN) {
                ESP_LOGI(GATTC_TAG, "Found UNKNOWN Service (FFE0)");
                h_unk_start = p_data->search_res.start_handle;
                h_unk_end   = p_data->search_res.end_handle;
            }
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Service search failed");
            break;
        }
        ESP_LOGI(GATTC_TAG, "Service search complete. Now looking for characteristics...");

        // Helper variables
        uint16_t count = 0;
        esp_gattc_char_elem_t *char_result = NULL;
        esp_bt_uuid_t char_uuid;
        char_uuid.len = ESP_UUID_LEN_16;

        // --- 1. OBSŁUGA BATERII (2A19) ---
        if (h_batt_start > 0) {
            char_uuid.uuid.uuid16 = UUID_CHAR_BATTERY;
            count = 1; // Szukamy jednej
            char_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
            
            if (esp_ble_gattc_get_char_by_uuid(gattc_if, p_data->search_cmpl.conn_id, h_batt_start, h_batt_end, char_uuid, char_result, &count) == ESP_GATT_OK && count > 0) {
                h_char_batt = char_result[0].char_handle;
                ESP_LOGI(GATTC_TAG, "Setup Battery: Read & Notify");
                
                // Odczyt (bo bateria ma Read)
                esp_ble_gattc_read_char(gattc_if, p_data->search_cmpl.conn_id, h_char_batt, ESP_GATT_AUTH_REQ_NONE);
                // Rejestracja powiadomień
                esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, h_char_batt);
            }
            free(char_result);
        }

        // --- 2. OBSŁUGA ALERTU (2A06) ---
        if (h_alert_start > 0) {
            char_uuid.uuid.uuid16 = UUID_CHAR_ALERT;
            count = 1;
            char_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);

            if (esp_ble_gattc_get_char_by_uuid(gattc_if, p_data->search_cmpl.conn_id, h_alert_start, h_alert_end, char_uuid, char_result, &count) == ESP_GATT_OK && count > 0) {
                h_char_alert = char_result[0].char_handle;
                ESP_LOGI(GATTC_TAG, "Setup Alert: Notify only (No Read)");
                // 2A06 zazwyczaj nie ma Read, tylko Write/Notify
                esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, h_char_alert);
            }
            free(char_result);
        }

        // --- 3. OBSŁUGA UNKNOWN/BUTTON (FFE1) ---
        if (h_unk_start > 0) {
            char_uuid.uuid.uuid16 = UUID_CHAR_UNKNOWN;
            count = 1;
            char_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);

            if (esp_ble_gattc_get_char_by_uuid(gattc_if, p_data->search_cmpl.conn_id, h_unk_start, h_unk_end, char_uuid, char_result, &count) == ESP_GATT_OK && count > 0) {
                h_char_unk = char_result[0].char_handle;
                ESP_LOGI(GATTC_TAG, "Setup Unknown: Read & Notify");
                
                // Odczyt
                esp_ble_gattc_read_char(gattc_if, p_data->search_cmpl.conn_id, h_char_unk, ESP_GATT_AUTH_REQ_NONE);
                // Rejestracja powiadomień
                esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, h_char_unk);
            }
            free(char_result);
        }
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        ESP_LOGI(GATTC_TAG, "Registered notify for handle %d. Writing CCCD...", p_data->reg_for_notify.handle);
        
        uint16_t notify_en = 1;
        uint16_t count = 0;
        esp_gattc_descr_elem_t *descr_elem_result = NULL;
        esp_bt_uuid_t notify_descr_uuid = { .len = ESP_UUID_LEN_16, .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG} };

        count = 1;
        descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
        if (descr_elem_result) {
            // POPRAWKA: Używamy globalnego conn_id z gl_profile_tab
            esp_err_t ret = esp_ble_gattc_get_descr_by_char_handle(gattc_if, 
                                                                    gl_profile_tab[PROFILE_A_APP_ID].conn_id, 
                                                                    p_data->reg_for_notify.handle, 
                                                                    notify_descr_uuid, 
                                                                    descr_elem_result, 
                                                                    &count);
            if (ret == ESP_GATT_OK && count > 0) {
                // POPRAWKA: Tutaj również używamy globalnego conn_id
                esp_ble_gattc_write_char_descr(gattc_if, 
                                               gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                               descr_elem_result[0].handle, 
                                               sizeof(notify_en), 
                                               (uint8_t *)&notify_en,
                                               ESP_GATT_WRITE_TYPE_RSP, 
                                               ESP_GATT_AUTH_REQ_NONE);
            }
            free(descr_elem_result);
        }
        break;
    }

    /* --- ODBIERANIE DANYCH --- */
    case ESP_GATTC_READ_CHAR_EVT:
    case ESP_GATTC_NOTIFY_EVT: {
        uint16_t handle = (event == ESP_GATTC_READ_CHAR_EVT) ? p_data->read.handle : p_data->notify.handle;
        uint8_t *val = (event == ESP_GATTC_READ_CHAR_EVT) ? p_data->read.value : p_data->notify.value;
        uint16_t len = (event == ESP_GATTC_READ_CHAR_EVT) ? p_data->read.value_len : p_data->notify.value_len;

        if (len > 0) {
            if (handle == h_char_batt) {
                ESP_LOGI(GATTC_TAG, ">>> [BATERIA] Poziom: %d %%", val[0]);
            } 
            else if (handle == h_char_alert) {
                ESP_LOGI(GATTC_TAG, ">>> [ALERT] Zmiana stanu! (Hex: %02x)", val[0]);
            } 
            else if (handle == h_char_unk) {
                // --- LOGIKA ZLICZANIA KLIKNIĘĆ ---
                ESP_LOGI(GATTC_TAG, ">>> [PRZYCISK] Otrzymano sygnał: %02x", val[0]);
                
                // Zakładamy, że 0x01 to kliknięcie (single click)
                if (val[0] == 0x01) {
                    click_count++;
                    ESP_LOGI(GATTC_TAG, "Licznik kliknięć: %d / 5", click_count);

                    if (click_count >= 5) {
                        ESP_LOGW(GATTC_TAG, "!!! 5 KLIKNIĘĆ OSIĄGNIĘTE - ZLECENIE ALARMU !!!");
                        click_count = 0;      // Reset licznika
                        trigger_alarm = true; // Ustaw flagę, app_main ją obsłuży
                    }
                }
            } 
        }
        break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p_data->write.status == ESP_GATT_OK) {
            ESP_LOGI(GATTC_TAG, "Notifications enabled successfully for a characteristic.");
        }
        break;
    
    case ESP_GATTC_DISCONNECT_EVT:
        connect = false;
        // Resetujemy uchwyty po rozłączeniu, żeby przy ponownym połączeniu szukać ich od nowa
        h_batt_start = 0; h_alert_start = 0; h_unk_start = 0;
        h_char_batt = INVALID_HANDLE; h_char_alert = INVALID_HANDLE; h_char_unk = INVALID_HANDLE;
        
        ESP_LOGI(GATTC_TAG, "Disconnected. Reason: 0x%x", p_data->disconnect.reason);
        ESP_LOGI(GATTC_TAG, "Restarting scanning in 30 seconds duration...");
        
        // --- KLUCZOWA ZMIANA: Wznawiamy skanowanie ---
        // Dzięki temu ESP32 wróci do "nasłuchiwania" i jak tylko tag się pojawi,
        // kod w esp_gap_cb (SCAN_RESULT) ponownie zainicjuje połączenie.
        esp_ble_gap_start_scanning(30); 
        break;

    default:
        break;
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        // The unit of duration is seconds.
        // If duration is set to 0, scanning will continue indefinitely
        // until esp_ble_gap_stop_scanning is explicitly called.
        uint32_t duration = 30;
        esp_ble_gap_start_scanning(duration);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "Scanning start failed, status %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Scanning start successfully");

        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            /* Wypisujemy logi, żeby widzieć co się dzieje */
            ESP_LOG_BUFFER_HEX(GATTC_TAG, scan_result->scan_rst.bda, 6);
            ESP_LOGI(GATTC_TAG, "RSSI: %d", scan_result->scan_rst.rssi);

            /* DEFINIUJEMY ADRES MAC TWOJEGO iTAGA (spisany ze zdjęcia) */
            uint8_t target_mac[6] = {0xFF, 0xFF, 0x1B, 0x0A, 0xF9, 0x96};

            /* Sprawdzamy czy znalezione urządzenie ma ten konkretny adres MAC */
            if (memcmp(scan_result->scan_rst.bda, target_mac, 6) == 0) {
                ESP_LOGI(GATTC_TAG, ">>> ZNALEZIONO iTAG PO ADRESIE MAC! <<<");
                
                if (connect == false) {
                    connect = true;
                    ESP_LOGI(GATTC_TAG, "Connect to the remote device.");
                    esp_ble_gap_stop_scanning();
                    esp_ble_gatt_creat_conn_params_t creat_conn_params = {0};
                    memcpy(&creat_conn_params.remote_bda, scan_result->scan_rst.bda, ESP_BD_ADDR_LEN);
                    creat_conn_params.remote_addr_type = scan_result->scan_rst.ble_addr_type;
                    creat_conn_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
                    creat_conn_params.is_direct = true;
                    creat_conn_params.is_aux = false;
                    creat_conn_params.phy_mask = 0x0;
                    esp_ble_gattc_enh_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                                        &creat_conn_params);
                }
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Scanning stop failed, status %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Scanning stop successfully");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Advertising stop failed, status %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(GATTC_TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                  param->update_conn_params.status,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
        ESP_LOGI(GATTC_TAG, "Packet length update, status %d, rx %d, tx %d",
                  param->pkt_data_length_cmpl.status,
                  param->pkt_data_length_cmpl.params.rx_len,
                  param->pkt_data_length_cmpl.params.tx_len);
        break;
    default:
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

void app_main(void)
{
    // --- Inicjalizacja (bez zmian) ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { return; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) { return; }

    ret = esp_bluedroid_init();
    if (ret) { return; }

    ret = esp_bluedroid_enable();
    if (ret) { return; }

    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){ return; }

    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){ return; }

    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){ }

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){ }

    // --- PĘTLA OBSŁUGI ZDARZEŃ ---
    ESP_LOGI(GATTC_TAG, "Start pętli głównej. Czekam na 5 kliknięć...");

    while (1) {
        // Sprawdzamy czy flaga została ustawiona w callbacku
        if (trigger_alarm) {
            
            // Upewniamy się, że nadal jesteśmy połączeni
            if (connect && h_char_alert != INVALID_HANDLE) {
                
                // 1. WŁĄCZ ALARM (High lub Mild)
                uint8_t write_val = 0x02;
                ESP_LOGW(GATTC_TAG, ">>> ALARM START! Typ: 0x%02x (Trwa 5s) <<<", write_val);
                
                esp_ble_gattc_write_char(
                    gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                    gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                    h_char_alert,
                    sizeof(write_val),
                    &write_val,
                    ESP_GATT_WRITE_TYPE_NO_RSP,
                    ESP_GATT_AUTH_REQ_NONE
                );

                // 2. CZEKAJ 5 SEKUND
                vTaskDelay(5000 / portTICK_PERIOD_MS);

                // 3. WYŁĄCZ ALARM
                ESP_LOGI(GATTC_TAG, ">>> ALARM STOP (Wysylanie 0x00) <<<");
                write_val = 0x00; // No Alert
                
                esp_ble_gattc_write_char(
                    gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                    gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                    h_char_alert,
                    sizeof(write_val),
                    &write_val,
                    ESP_GATT_WRITE_TYPE_NO_RSP,
                    ESP_GATT_AUTH_REQ_NONE
                );

                // Reset flagi - czekamy na kolejne 5 kliknięć
                trigger_alarm = false;

            } else {
                ESP_LOGW(GATTC_TAG, "Nie można włączyć alarmu - brak połączenia");
                trigger_alarm = false; // Resetujemy, żeby nie odpaliło się od razu po ponownym połączeniu
            }
        }

        // Krótkie opóźnienie w pętli, żeby nie zjadać 100% CPU
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}