#include "wifi_prov.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_err.h"
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
#include "esp_mac.h"

#include "esp_attr.h"

#include "mqtt_app.h"
#include "alert_limiter.h"

#define LOG_TAG "WIFI_PROV"

#define FACTORY_RESET_MAGIC 0x53475246u // 'SGRF'
RTC_NOINIT_ATTR static uint32_t s_factory_reset_marker = 0;

// --- KONFIGURACJA GPIO (BOOT Button) ---
#define BUTTON_GPIO GPIO_NUM_0 
#define BUTTON_HOLD_RESET_MS 3000

// --- BLE UUID ---
#define SERVICE_UUID        {0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef}
#define CHAR_SSID_UUID      0xFF01 
#define CHAR_PASS_UUID      0xFF02
#define CHAR_CTRL_UUID      0xFF03
#define CHAR_BROKER_UUID    0xFF04
#define CHAR_MQTT_LOGIN_UUID 0xFF05
#define CHAR_MQTT_PASS_UUID 0xFF06
#define CHAR_USER_ID_UUID   0xFF07
#define CHAR_DEVICE_ID_UUID 0xFF08

// --- NVS Keys ---
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define NVS_KEY_BROKER "broker_uri"
#define NVS_KEY_MQTT_LOGIN "mqtt_login"
#define NVS_KEY_MQTT_PASS "mqtt_pass"
#define NVS_KEY_USER_ID "user_id"

// --- Zmienne globalne ---
static EventGroupHandle_t s_wifi_event_group;
// Bit w grupie zdarzeń oznaczający, że mamy połączenie (IP)
#define WIFI_CONNECTED_BIT BIT0

static uint16_t ssid_handle, pass_handle, ctrl_handle;
static uint16_t broker_handle, mqtt_login_handle, mqtt_pass_handle, user_id_handle, device_id_handle;
static bool restart_pending = false;

static char temp_ssid[32] = {0};
static char temp_pass[64] = {0};
static char temp_broker[128] = {0};
static char temp_mqtt_login[64] = {0};
static char temp_mqtt_pass[64] = {0};
static char temp_user_id[64] = {0};

static bool ssid_dirty = false;
static bool pass_dirty = false;
static bool broker_dirty = false;
static bool mqtt_login_dirty = false;
static bool mqtt_pass_dirty = false;
static bool user_id_dirty = false;

static esp_timer_handle_t prov_timeout_timer = NULL;
static bool provisioning_window_open = false;
static bool provisioning_done = false;
static bool ble_stack_started = false;
static volatile bool wifi_credentials_present = false;
static bool ble_client_connected = false;
static bool ble_adv_active = false;

static int s_ble_conn_id = -1;
static esp_bd_addr_t s_ble_remote_bda = {0};
static bool s_ble_remote_bda_valid = false;

// adv_params jest definiowane niżej (sekcja BLE Logic), ale helper request_advertising_start() używa go wcześniej.
static esp_ble_adv_params_t adv_params;

static void get_device_id_mac_hex(char *out, size_t out_len) {
    if (!out || out_len < 13) {
        if (out && out_len > 0) out[0] = '\0';
        return;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static TaskHandle_t s_prov_ctrl_task_handle = NULL;

#define PROV_ADV_TIMEOUT_MS (2 * 60 * 1000) // 2 minuty na konfigurację

// --- Deklaracje wewn. ---
static void start_provisioning_window(void);
static void stop_ble_provisioning(void);
static void close_provisioning_window(bool provisioning_completed);
static void start_ble_stack(void);
static void connect_wifi(const char* ssid, const char* pass);
static void prov_ctrl_task(void *pvParameter);
static void start_prov_timeout_if_needed(void);
static void stop_prov_timeout(void);
static void log_missing_required_fields(const char *reason);
static void log_ble_state(const char *where);
static void request_advertising_start(const char *reason);

// --------------------------------------------------------------------------
// 1. HELPERS (Timer, NVS)
// --------------------------------------------------------------------------

static esp_timer_handle_t s_reconnect_timer = NULL;

static void reconnect_timer_cb(void *arg) {
    ESP_LOGI(LOG_TAG, "Reconnect timer expired. Triggering connection attempt...");
    esp_wifi_connect();
}

static void restart_timer_cb(void *arg) {
    ESP_LOGI(LOG_TAG, "Restart timer expired. Rebooting system now.");
    esp_restart();
}

static void provisioning_timeout_cb(void *arg) {
    (void)arg;
    if (provisioning_window_open && !provisioning_done && ble_adv_active && !ble_client_connected) {
        ESP_LOGW(LOG_TAG, "Provisioning timeout (%d ms). Stopping advertising...", PROV_ADV_TIMEOUT_MS);
        close_provisioning_window(false);
        ESP_LOGW(LOG_TAG, "Provisioning incomplete. Device will not start measurements until configured.");

        log_missing_required_fields("timeout");

        if (alert_limiter_allow("provisioning.timeout", esp_log_timestamp(), 10 * 60 * 1000, NULL)) {
            mqtt_app_send_alert2("provisioning.timeout", "warning", "provisioning", "Provisioning window timed out");
        }
    }
}

static void log_missing_required_fields(const char *reason) {
    wifi_prov_config_t cfg;
    if (wifi_prov_get_config(&cfg) != ESP_OK) {
        ESP_LOGW(LOG_TAG,
                 "Unable to read provisioning config from NVS to list missing fields (%s).",
                 reason ? reason : "?");
        return;
    }

    bool missing_ssid = (cfg.ssid[0] == '\0');
    bool missing_broker = (cfg.broker_uri[0] == '\0');
    bool missing_mqtt_login = (cfg.mqtt_login[0] == '\0');
    bool missing_mqtt_pass = (cfg.mqtt_pass[0] == '\0');
    bool missing_user_id = (cfg.user_id[0] == '\0');

    if (missing_ssid || missing_broker || missing_mqtt_login || missing_mqtt_pass || missing_user_id) {
        ESP_LOGW(LOG_TAG,
                 "Missing provisioning fields (%s):%s%s%s%s%s",
                 reason ? reason : "?",
                 missing_ssid ? " ssid" : "",
                 missing_broker ? " broker_uri" : "",
                 missing_mqtt_login ? " mqtt_login" : "",
                 missing_mqtt_pass ? " mqtt_pass" : "",
                 missing_user_id ? " user_id" : "");
    } else {
        ESP_LOGI(LOG_TAG, "All required provisioning fields are present (%s).", reason ? reason : "?");
    }
}

static void log_ble_state(const char *where) {
    if (s_ble_remote_bda_valid) {
        ESP_LOGI(LOG_TAG,
                 "BLE state (%s): win_open=%d done=%d adv=%d connected=%d conn_id=%d remote=" ESP_BD_ADDR_STR,
                 where ? where : "?",
                 provisioning_window_open,
                 provisioning_done,
                 ble_adv_active,
                 ble_client_connected,
                 s_ble_conn_id,
                 ESP_BD_ADDR_HEX(s_ble_remote_bda));
    } else {
        ESP_LOGI(LOG_TAG,
                 "BLE state (%s): win_open=%d done=%d adv=%d connected=%d conn_id=%d",
                 where ? where : "?",
                 provisioning_window_open,
                 provisioning_done,
                 ble_adv_active,
                 ble_client_connected,
                 s_ble_conn_id);
    }
}

static void request_advertising_start(const char *reason) {
    if (!provisioning_window_open || provisioning_done) {
        ESP_LOGI(LOG_TAG, "Skip advertising start (%s): window_open=%d done=%d",
                 reason ? reason : "?", provisioning_window_open, provisioning_done);
        return;
    }
    if (ble_client_connected) {
        ESP_LOGI(LOG_TAG, "Skip advertising start (%s): client already connected (conn_id=%d)",
                 reason ? reason : "?", s_ble_conn_id);
        return;
    }
    if (ble_adv_active) {
        ESP_LOGI(LOG_TAG, "Skip advertising start (%s): advertising already active", reason ? reason : "?");
        return;
    }

    ESP_LOGI(LOG_TAG, "Starting advertising (%s)...", reason ? reason : "?");
    esp_err_t err = esp_ble_gap_start_advertising(&adv_params);
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "esp_ble_gap_start_advertising failed (%s): %s", reason ? reason : "?", esp_err_to_name(err));
    }
}

static void stop_prov_timeout(void) {
    if (prov_timeout_timer) {
        esp_timer_stop(prov_timeout_timer);
    }
}

static void start_prov_timeout_if_needed(void) {
    if (!provisioning_window_open || provisioning_done) return;
    if (ble_client_connected) return;
    if (!ble_adv_active) return;
    if (!prov_timeout_timer) return;

    esp_timer_stop(prov_timeout_timer);
    esp_timer_start_once(prov_timeout_timer, (int64_t)PROV_ADV_TIMEOUT_MS * 1000);
}

static esp_err_t save_prov_settings_partial(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    if (ssid_dirty && temp_ssid[0] != '\0') {
        err = nvs_set_str(my_handle, NVS_KEY_SSID, temp_ssid);
        if (err != ESP_OK) goto out;
    }
    if (pass_dirty && temp_pass[0] != '\0') {
        err = nvs_set_str(my_handle, NVS_KEY_PASS, temp_pass);
        if (err != ESP_OK) goto out;
    }
    if (broker_dirty && temp_broker[0] != '\0') {
        err = nvs_set_str(my_handle, NVS_KEY_BROKER, temp_broker);
        if (err != ESP_OK) goto out;
    }
    if (mqtt_login_dirty && temp_mqtt_login[0] != '\0') {
        err = nvs_set_str(my_handle, NVS_KEY_MQTT_LOGIN, temp_mqtt_login);
        if (err != ESP_OK) goto out;
    }
    if (mqtt_pass_dirty && temp_mqtt_pass[0] != '\0') {
        err = nvs_set_str(my_handle, NVS_KEY_MQTT_PASS, temp_mqtt_pass);
        if (err != ESP_OK) goto out;
    }
    if (user_id_dirty && temp_user_id[0] != '\0') {
        err = nvs_set_str(my_handle, NVS_KEY_USER_ID, temp_user_id);
        if (err != ESP_OK) goto out;
    }

    err = nvs_commit(my_handle);

out:
    nvs_close(my_handle);

    // Odśwież flagę obecności WiFi creds na podstawie SSID w NVS (po commit)
    if (err == ESP_OK) {
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            size_t ssid_len = 0;
            esp_err_t e2 = nvs_get_str(h, NVS_KEY_SSID, NULL, &ssid_len);
            if (e2 == ESP_OK && ssid_len > 1) {
                wifi_credentials_present = true;
            } else {
                wifi_credentials_present = false;
            }
            nvs_close(h);
        }
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
        ESP_LOGI(LOG_TAG, "NVS Wifi Config cleared.");
    }
    return err;
}

static void reset_temp_buffers_and_flags(void) {
    memset(temp_ssid, 0, sizeof(temp_ssid));
    memset(temp_pass, 0, sizeof(temp_pass));
    memset(temp_broker, 0, sizeof(temp_broker));
    memset(temp_mqtt_login, 0, sizeof(temp_mqtt_login));
    memset(temp_mqtt_pass, 0, sizeof(temp_mqtt_pass));
    memset(temp_user_id, 0, sizeof(temp_user_id));

    ssid_dirty = pass_dirty = broker_dirty = mqtt_login_dirty = mqtt_pass_dirty = user_id_dirty = false;
}


// --------------------------------------------------------------------------
// 2. WiFi Logic
// --------------------------------------------------------------------------

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Nie łączymy się tutaj automatycznie, bo connect_wifi() ustawi config i wywoła connect ręcznie.
        // esp_wifi_connect(); 
        ESP_LOGI(LOG_TAG, "WiFi Started. Waiting for configuration...");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(LOG_TAG, "WiFi Disconnected. Retrying...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        uint32_t suppressed = 0;
        if (alert_limiter_allow("wifi.disconnected", esp_log_timestamp(), 60 * 1000, &suppressed)) {
            int reason = -1;
            if (event_data) {
                wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
                reason = (int)d->reason;
            }
            char details[128];
            snprintf(details, sizeof(details), "{\"reason\":%d,\"suppressed\":%lu}", reason, (unsigned long)suppressed);
            mqtt_app_send_alert2_details("wifi.disconnected", "warning", "wifi", "WiFi disconnected. Retrying in 30s...", details);
        }

        // Zamiast natychmiastowego reconnectu, czekamy 30s
        if (s_reconnect_timer) {
            esp_timer_stop(s_reconnect_timer); // Reset jeśli już leci
            esp_timer_start_once(s_reconnect_timer, 30000000); // 30 sekund
        } else {
            // Fallback (powinno być zainicjalizowane)
            esp_wifi_connect(); 
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(LOG_TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (alert_limiter_allow("wifi.got_ip", esp_log_timestamp(), 5 * 60 * 1000, NULL)) {
            mqtt_app_send_alert2("wifi.got_ip", "info", "wifi", "WiFi got IP");
        }
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
        ESP_LOGI(LOG_TAG, "BLE adv data set complete");
        request_advertising_start("adv_data_set_complete");
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(LOG_TAG, "Advertising start failed (status=%d)", param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(LOG_TAG, "BLE Advertising started");
            ble_adv_active = true;
            start_prov_timeout_if_needed();
        }
        log_ble_state("adv_start_complete");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ble_adv_active = false;
        ESP_LOGI(LOG_TAG, "BLE Advertising stopped (status=%d)", param->adv_stop_cmpl.status);
        log_ble_state("adv_stop_complete");
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

        // Potrzebujemy wystarczającej liczby handle'i na: service + (deklaracja+wartość) dla każdej charakterystyki.
        // Mamy 7 charakterystyk, więc 10 to za mało i kolejne add_char mogą się nie pojawić w kliencie.
        esp_ble_gatts_create_service(gatts_if, &service_id, 30);
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        uint16_t service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(service_handle);

        esp_bt_uuid_t char_uuid;
        char_uuid.len = ESP_UUID_LEN_16;

        // SSID (READ+WRITE)
        char_uuid.uuid.uuid16 = CHAR_SSID_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                       ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
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

        // BROKER (READ+WRITE)
        char_uuid.uuid.uuid16 = CHAR_BROKER_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                       ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // MQTT LOGIN (device_id)
        char_uuid.uuid.uuid16 = CHAR_MQTT_LOGIN_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // MQTT PASS
        char_uuid.uuid.uuid16 = CHAR_MQTT_PASS_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // USER ID
        char_uuid.uuid.uuid16 = CHAR_USER_ID_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);

        // DEVICE ID (MAC) - READ ONLY
        char_uuid.uuid.uuid16 = CHAR_DEVICE_ID_UUID;
        esp_ble_gatts_add_char(service_handle, &char_uuid,
                               ESP_GATT_PERM_READ,
                               ESP_GATT_CHAR_PROP_BIT_READ,
                               NULL, NULL);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t uuid = param->add_char.char_uuid.uuid.uuid16;
        if (uuid == CHAR_SSID_UUID) ssid_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_PASS_UUID) pass_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_CTRL_UUID) ctrl_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_BROKER_UUID) broker_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_MQTT_LOGIN_UUID) mqtt_login_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_MQTT_PASS_UUID) mqtt_pass_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_USER_ID_UUID) user_id_handle = param->add_char.attr_handle;
        else if (uuid == CHAR_DEVICE_ID_UUID) device_id_handle = param->add_char.attr_handle;
        break;
    }

    case ESP_GATTS_READ_EVT: {
        const char *which = "unknown";
        if (param->read.handle == ssid_handle) which = "ssid";
        else if (param->read.handle == broker_handle) which = "broker_uri";
        else if (param->read.handle == device_id_handle) which = "device_id";

        ESP_LOGI(LOG_TAG, "BLE READ: %s (handle=0x%04x, conn_id=%d)", which, param->read.handle, param->read.conn_id);

        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.attr_value.handle = param->read.handle;

        if (param->read.handle == ssid_handle || param->read.handle == broker_handle) {
            wifi_prov_config_t cfg;
            if (wifi_prov_get_config(&cfg) == ESP_OK) {
                const char *val = "";
                if (param->read.handle == ssid_handle) {
                    val = cfg.ssid;
                } else if (param->read.handle == broker_handle) {
                    val = cfg.broker_uri;
                }

                size_t vlen = strlen(val);
                if (vlen > (sizeof(rsp.attr_value.value))) vlen = sizeof(rsp.attr_value.value);
                rsp.attr_value.len = (uint16_t)vlen;
                memcpy(rsp.attr_value.value, val, vlen);
            }
        } else if (param->read.handle == device_id_handle) {
            char dev_id[13] = {0};
            get_device_id_mac_hex(dev_id, sizeof(dev_id));

            size_t vlen = strlen(dev_id);
            if (vlen > (sizeof(rsp.attr_value.value))) vlen = sizeof(rsp.attr_value.value);
            rsp.attr_value.len = (uint16_t)vlen;
            memcpy(rsp.attr_value.value, dev_id, vlen);
        } else {
            rsp.attr_value.len = 0;
        }

        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }
    case ESP_GATTS_CONNECT_EVT:
        if (ble_client_connected) {
            ESP_LOGW(LOG_TAG, "BLE CONNECT_EVT while already connected (old_conn_id=%d)", s_ble_conn_id);
        }

        s_ble_conn_id = param->connect.conn_id;
        memcpy(s_ble_remote_bda, param->connect.remote_bda, sizeof(s_ble_remote_bda));
        s_ble_remote_bda_valid = true;

        ESP_LOGI(LOG_TAG, "BLE Client Connected: conn_id=%d remote=" ESP_BD_ADDR_STR,
                 param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
        ble_client_connected = true;
        ble_adv_active = false; // advertising zwykle przestaje działać po zestawieniu połączenia
        stop_prov_timeout();
        log_ble_state("connect_evt");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(LOG_TAG, "BLE Client Disconnected: conn_id=%d reason=0x%02x remote=" ESP_BD_ADDR_STR,
                 param->disconnect.conn_id,
                 param->disconnect.reason,
                 ESP_BD_ADDR_HEX(param->disconnect.remote_bda));
        ble_client_connected = false;
        ble_adv_active = false;
        s_ble_conn_id = -1;
        s_ble_remote_bda_valid = false;
        if (restart_pending) {
            ESP_LOGI(LOG_TAG, "Restart pending. Waiting 1s...");
            esp_timer_create_args_t timer_args = { .callback = &restart_timer_cb, .name = "restart_timer" };
            esp_timer_handle_t r_timer;
            esp_timer_create(&timer_args, &r_timer);
            esp_timer_start_once(r_timer, 1000000);
        } else if (provisioning_window_open && !provisioning_done) {
            request_advertising_start("disconnect_evt");
        }
        log_ble_state("disconnect_evt");
        break;

    case ESP_GATTS_CLOSE_EVT:
        ESP_LOGI(LOG_TAG, "BLE connection closed (conn_id=%d)", param->close.conn_id);

        // Zdarza się, że część klientów/stacków kończy połączenie CLOSE_EVT bez DISCONNECT_EVT.
        // Żeby nie utknąć w stanie "połączone" i bez advertisingu, traktujemy CLOSE_EVT jako rozłączenie.
        if (ble_client_connected || s_ble_conn_id != -1) {
            ble_client_connected = false;
            ble_adv_active = false;
            s_ble_conn_id = -1;
            s_ble_remote_bda_valid = false;

            if (!restart_pending && provisioning_window_open && !provisioning_done) {
                request_advertising_start("close_evt");
            }
        }

        log_ble_state("close_evt");
        break;

    case ESP_GATTS_WRITE_EVT: {
        if (param->write.handle == ssid_handle) {
            memset(temp_ssid, 0, sizeof(temp_ssid));
            size_t len = (param->write.len < sizeof(temp_ssid)-1) ? param->write.len : sizeof(temp_ssid)-1;
            memcpy(temp_ssid, param->write.value, len);
            ESP_LOGI(LOG_TAG, "SSID rcv: %s", temp_ssid);
            ssid_dirty = true;
        } 
        else if (param->write.handle == pass_handle) {
            memset(temp_pass, 0, sizeof(temp_pass));
            size_t len = (param->write.len < sizeof(temp_pass)-1) ? param->write.len : sizeof(temp_pass)-1;
            memcpy(temp_pass, param->write.value, len);
            ESP_LOGI(LOG_TAG, "PASS rcv: ***");
            pass_dirty = true;
        } 
        else if (param->write.handle == broker_handle) {
            memset(temp_broker, 0, sizeof(temp_broker));
            size_t len = (param->write.len < sizeof(temp_broker)-1) ? param->write.len : sizeof(temp_broker)-1;
            memcpy(temp_broker, param->write.value, len);
            ESP_LOGI(LOG_TAG, "BROKER rcv: %s", temp_broker);
            broker_dirty = true;
        }
        else if (param->write.handle == mqtt_login_handle) {
            memset(temp_mqtt_login, 0, sizeof(temp_mqtt_login));
            size_t len = (param->write.len < sizeof(temp_mqtt_login)-1) ? param->write.len : sizeof(temp_mqtt_login)-1;
            memcpy(temp_mqtt_login, param->write.value, len);
            ESP_LOGI(LOG_TAG, "MQTT LOGIN rcv: %s", temp_mqtt_login);
            mqtt_login_dirty = true;
        }
        else if (param->write.handle == mqtt_pass_handle) {
            memset(temp_mqtt_pass, 0, sizeof(temp_mqtt_pass));
            size_t len = (param->write.len < sizeof(temp_mqtt_pass)-1) ? param->write.len : sizeof(temp_mqtt_pass)-1;
            memcpy(temp_mqtt_pass, param->write.value, len);
            ESP_LOGI(LOG_TAG, "MQTT PASS rcv: ***");
            mqtt_pass_dirty = true;
        }
        else if (param->write.handle == user_id_handle) {
            memset(temp_user_id, 0, sizeof(temp_user_id));
            size_t len = (param->write.len < sizeof(temp_user_id)-1) ? param->write.len : sizeof(temp_user_id)-1;
            memcpy(temp_user_id, param->write.value, len);
            ESP_LOGI(LOG_TAG, "USER ID rcv: %s", temp_user_id);
            user_id_dirty = true;
        }
        else if (param->write.handle == ctrl_handle) {
            // Potwierdzenie jako bajt 0x01 (nie jako string "1")
            if (param->write.len == 1 && param->write.value[0] == 0x01) {
                ESP_LOGI(LOG_TAG, "Saving provisioning settings (partial) & Rebooting...");
                provisioning_done = true;
                close_provisioning_window(true);
                esp_err_t err = save_prov_settings_partial();
                if (err != ESP_OK) {
                    ESP_LOGE(LOG_TAG, "Failed to save provisioning settings: %s", esp_err_to_name(err));

                    uint32_t suppressed = 0;
                    if (alert_limiter_allow("provisioning.save_failed", esp_log_timestamp(), 5 * 60 * 1000, &suppressed)) {
                        char details[128];
                        snprintf(details, sizeof(details), "{\"err\":%d,\"suppressed\":%lu}", (int)err, (unsigned long)suppressed);
                        mqtt_app_send_alert2_details("provisioning.save_failed", "error", "provisioning", "Failed to save provisioning settings", details);
                    }
                }
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
    ESP_LOGI(LOG_TAG, "Stopping advertising (stop_ble_provisioning)...");
    esp_ble_gap_stop_advertising();
    ble_adv_active = false;
    log_ble_state("stop_ble_provisioning");
}

static void close_provisioning_window(bool completed) {
    ESP_LOGI(LOG_TAG, "Closing provisioning window (completed=%d)", completed);
    provisioning_window_open = false;
    provisioning_done = completed;
    stop_prov_timeout();
    stop_ble_provisioning();
    log_ble_state("close_provisioning_window");
}

static void start_provisioning_window(void) {
    reset_temp_buffers_and_flags();
    provisioning_done = false;
    provisioning_window_open = true;
    // Nie resetujemy ble_client_connected tutaj: short-click może nadejść, gdy klient już jest podłączony.
    // Resetowanie flagi rozwala spójność logów i stanu (CONNECT/DISCONNECT).

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
    // Timeout uruchamiamy dopiero gdy advertising faktycznie ruszy i nikt nie jest połączony.
    stop_prov_timeout();

    if (!ble_stack_started) {
        ble_stack_started = true;
        start_ble_stack();
    } else {
        request_advertising_start("start_provisioning_window");
    }

    log_ble_state("start_provisioning_window");
}

// --------------------------------------------------------------------------
// 5. Button Task
// --------------------------------------------------------------------------

static void prov_ctrl_task(void *pvParameter) {
    (void)pvParameter;
    while (1) {
        // Czekamy na sygnał z button_task (krótki klik)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(LOG_TAG, "Provisioning requested (button).");
        log_ble_state("button_notify");
        start_provisioning_window();
    }
}

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
                    // Keep a marker across reset so we can report it later (MQTT may be unavailable now).
                    s_factory_reset_marker = FACTORY_RESET_MAGIC;
                    clear_wifi_credentials();
                    esp_restart();
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            // Short Click
            int64_t press_ms = (esp_timer_get_time() - start_us) / 1000;
            if (press_ms < BUTTON_HOLD_RESET_MS) {
                ESP_LOGI(LOG_TAG, "Click -> Start/Restart Config Window (always).");
                log_ble_state("button_click");
                if (s_prov_ctrl_task_handle) {
                    xTaskNotifyGive(s_prov_ctrl_task_handle);
                } else {
                    // Fallback jeśli task nie wystartował
                    start_provisioning_window();
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

    if (s_factory_reset_marker == FACTORY_RESET_MAGIC) {
        s_factory_reset_marker = 0;
        if (alert_limiter_once("system.factory_reset")) {
            mqtt_app_send_alert2("system.factory_reset", "warning", "system", "Factory reset requested via button");
        }
    }
    
    // Start button task
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // Timer do reconnectu WiFi (30s)
    esp_timer_create_args_t recon_args = {
        .callback = &reconnect_timer_cb,
        .name = "wifi_reconnect"
    };
    esp_timer_create(&recon_args, &s_reconnect_timer);

    // Task, który odpala provisioning/BLE (żeby nie przepełniać stosu w button_task)
    xTaskCreate(prov_ctrl_task, "prov_ctrl_task", 4096, NULL, 9, &s_prov_ctrl_task_handle);

    // Load Creds
    char ssid[32] = {0};
    char pass[64] = {0};
    esp_err_t err = load_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    wifi_credentials_present = (err == ESP_OK && strlen(ssid) > 0);

    if (wifi_credentials_present) {
        ESP_LOGI(LOG_TAG, "Found stored credentials. Connecting...");
        wifi_init_sta();
        connect_wifi(ssid, pass);
    }

    // Advertising ma odpalić zawsze, jeśli brakuje któregokolwiek z wymaganych pól.
    if (!wifi_prov_is_fully_provisioned()) {
        ESP_LOGW(LOG_TAG, "Provisioning incomplete. Starting BLE provisioning window...");
        log_missing_required_fields("boot");

        if (alert_limiter_once("provisioning.incomplete")) {
            mqtt_app_send_alert2("provisioning.incomplete", "warning", "provisioning", "Device not fully provisioned. Measurements blocked until configured.");
        }

        start_provisioning_window();
    }
}

void wifi_prov_wait_connected(void) {
    // Czekaj na bit połączenia (bez timeoutu - blokuj do skutku)
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

esp_err_t wifi_prov_get_config(wifi_prov_config_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_NOT_INITIALIZED) {
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    size_t len;

    len = sizeof(out->ssid);
    if (nvs_get_str(h, NVS_KEY_SSID, out->ssid, &len) != ESP_OK) out->ssid[0] = '\0';

    len = sizeof(out->pass);
    if (nvs_get_str(h, NVS_KEY_PASS, out->pass, &len) != ESP_OK) out->pass[0] = '\0';

    len = sizeof(out->broker_uri);
    if (nvs_get_str(h, NVS_KEY_BROKER, out->broker_uri, &len) != ESP_OK) out->broker_uri[0] = '\0';

    len = sizeof(out->mqtt_login);
    if (nvs_get_str(h, NVS_KEY_MQTT_LOGIN, out->mqtt_login, &len) != ESP_OK) out->mqtt_login[0] = '\0';

    len = sizeof(out->mqtt_pass);
    if (nvs_get_str(h, NVS_KEY_MQTT_PASS, out->mqtt_pass, &len) != ESP_OK) out->mqtt_pass[0] = '\0';

    len = sizeof(out->user_id);
    if (nvs_get_str(h, NVS_KEY_USER_ID, out->user_id, &len) != ESP_OK) out->user_id[0] = '\0';

    nvs_close(h);
    return ESP_OK;
}

bool wifi_prov_is_fully_provisioned(void) {
    wifi_prov_config_t cfg;
    if (wifi_prov_get_config(&cfg) != ESP_OK) return false;

    // WiFi pass może być puste dla OPEN, ale SSID musi istnieć.
    if (cfg.ssid[0] == '\0') return false;

    if (cfg.broker_uri[0] == '\0') return false;
    if (cfg.mqtt_login[0] == '\0') return false;
    if (cfg.mqtt_pass[0] == '\0') return false;
    if (cfg.user_id[0] == '\0') return false;

    return true;
}

bool wifi_prov_is_provisioning_active(void) {
    // "Aktywny provisioning" rozumiemy jako: okno jest otwarte i provisioning nie został zakończony,
    // albo trwa advertising, albo klient BLE jest podłączony.
    if (provisioning_window_open && !provisioning_done) return true;
    if (ble_adv_active) return true;
    if (ble_client_connected) return true;
    return false;
}
