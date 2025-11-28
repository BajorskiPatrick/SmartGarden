#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* NimBLE Includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define GATTS_TAG "iTAG_SIM_NIMBLE"
#define DEVICE_NAME "iTAG_simulation"

/* --- ZMIENNE GLOBALNE STANU --- */
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool is_connected = false;

/* --- UCHWYTY DO WARTOŚCI CHARAKTERYSTYK (Value Handles) --- */
/* NimBLE automatycznie przypisze tu numery uchwytów po inicjalizacji serwisów */
static uint16_t h_batt_val_handle = 0;
static uint16_t h_alert_val_handle = 0;
static uint16_t h_btn_val_handle = 0;

/* Deklaracje funkcji */
void ble_store_config_init(void);

/* --- DEFINICJA OBSŁUGI ODCZYTU/ZAPISU (CALLBACKI) --- */

/**
 * Obsługa dostępu do charakterystyk (Read/Write)
 */
static int device_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* 1. Obsługa Baterii (UUID 0x2A19) - Tylko odczyt */
    if (attr_handle == h_batt_val_handle) {
        uint8_t batt_val = 99; // Stała wartość jak w oryginale
        ESP_LOGI(GATTS_TAG, "[BATT] Odczyt wartości: %d%%", batt_val);
        
        int rc = os_mbuf_append(ctxt->om, &batt_val, sizeof(batt_val));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    /* 2. Obsługa Alertu (UUID 0x2A06) - Zapis */
    if (attr_handle == h_alert_val_handle) {
        // Sprawdzamy czy to operacja zapisu
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            if (ctxt->om->om_len > 0) {
                uint8_t data = ctxt->om->om_data[0];
                ESP_LOGW(GATTS_TAG, "!!! ALARM TRIGGERED [2A06] !!! Received value: 0x%02x", data);
            }
            return 0; // Sukces
        }
    }

    /* 3. Obsługa Przycisku (UUID 0xFFE1) - Odczyt */
    if (attr_handle == h_btn_val_handle) {
        uint8_t btn_val = 0x01; // Stała wartość 0x01
        ESP_LOGI(GATTS_TAG, "[BTN] Odczyt wartości: 0x%02x", btn_val);

        int rc = os_mbuf_append(ctxt->om, &btn_val, sizeof(btn_val));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* --- TABLICA DEFINICJI SERWISÓW I CHARAKTERYSTYK --- */
/* To jest największa zaleta NimBLE - wszystko w jednej strukturze */

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* --- SERWIS 1: Battery Service (0x180F) --- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Characteristic: Battery Level (0x2A19) */
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = device_access_cb,
                .val_handle = &h_batt_val_handle, // Tu NimBLE zapisze uchwyt
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0, /* Koniec tablicy charakterystyk */
            }
        },
    },

    /* --- SERWIS 2: Immediate Alert (0x1802) --- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1802),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Characteristic: Alert Level (0x2A06) */
                .uuid = BLE_UUID16_DECLARE(0x2A06),
                .access_cb = device_access_cb,
                .val_handle = &h_alert_val_handle,
                /* Oryginał miał Write, WriteNR oraz Notify */
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0,
            }
        },
    },

    /* --- SERWIS 3: Button / Unknown (0xFFE0) --- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFE0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Characteristic: Button Status (0xFFE1) */
                .uuid = BLE_UUID16_DECLARE(0xFFE1),
                .access_cb = device_access_cb,
                .val_handle = &h_btn_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0,
            }
        },
    },

    {
        0, /* Koniec tablicy serwisów */
    },
};

/* --- TASK SYMULACYJNY (Główna logika czasowa) --- */
void simulation_task(void *pvParameters) {
    uint8_t btn_val = 0x01; // Kliknięcie
    uint8_t batt_val = 99;  // Bateria
    uint32_t loop_counter = 0;

    ESP_LOGI(GATTS_TAG, "Task symulacyjny wystartował.");

    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Co 5 sekund
        loop_counter++;

        if (is_connected) {
            // 1. Wyślij przycisk - ZAWSZE co 5 sekund
            if (h_btn_val_handle != 0) {
                ESP_LOGI(GATTS_TAG, "[SIM] Wysyłanie kliknięcia przycisku (FFE1)...");
                // W NimBLE używamy ble_gatts_notify_custom dla powiadomień
                struct os_mbuf *om = ble_hs_mbuf_from_flat(&btn_val, sizeof(btn_val));
                ble_gatts_notify_custom(conn_handle, h_btn_val_handle, om);
            }

            // 2. Wyślij baterię - CO DRUGI RAZ (czyli co 10 sekund)
            if (loop_counter % 2 == 0) {
                if (h_batt_val_handle != 0) {
                    ESP_LOGI(GATTS_TAG, "[SIM] Wysyłanie stanu baterii (2A19): %d%%", batt_val);
                    struct os_mbuf *om = ble_hs_mbuf_from_flat(&batt_val, sizeof(batt_val));
                    ble_gatts_notify_custom(conn_handle, h_batt_val_handle, om);
                    
                    // Symulacja rozładowywania
                    if(batt_val > 0) batt_val--;
                }
            }
        }
    }
}

/* --- OBSŁUGA ZDARZEŃ GAP (Połączenia, Rozgłaszanie) --- */

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(GATTS_TAG, "Połączono! Handle: %d", event->connect.conn_handle);
        conn_handle = event->connect.conn_handle;
        is_connected = true;
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(GATTS_TAG, "Rozłączono. Reason: 0x%x", event->disconnect.reason);
        is_connected = false;
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        
        // Wznów rozgłaszanie po rozłączeniu
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, NULL, ble_gap_event, NULL);
        return 0;

    default:
        return 0;
    }
}

/* Rozpoczęcie rozgłaszania */
void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /* Ustawienie danych rozgłoszeniowych */
    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    // TX Power
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // Nazwa urządzenia
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(GATTS_TAG, "Błąd ustawiania danych adv: %d", rc);
        return;
    }

    /* Ustawienie parametrów rozgłaszania */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // General discoverable
    
    // Intervaly (jednostka 0.625ms) - min 0x20, max 0x40 (zgodnie z oryginałem)
    adv_params.itvl_min = 0x20; 
    adv_params.itvl_max = 0x40;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(GATTS_TAG, "Błąd startu rozgłaszania: %d", rc);
    } else {
        ESP_LOGI(GATTS_TAG, "Rozgłaszanie jako: %s", DEVICE_NAME);
    }
}

/* Callback inicjalizacyjny */
void ble_app_on_sync(void)
{
    int rc;
    
    /* Upewnij się, że adres MAC jest wygenerowany */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Rozpocznij rozgłaszanie */
    ble_app_advertise();
}

/* Task dla Hosta NimBLE */
void nimble_host_task(void *param)
{
    ESP_LOGI(GATTS_TAG, "NimBLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    /* Inicjalizacja NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Inicjalizacja NimBLE */
    ESP_ERROR_CHECK(nimble_port_init());

    /* Ustawienie nazwy urządzenia */
    ble_svc_gap_device_name_set(DEVICE_NAME);

    /* Inicjalizacja serwisów z tablicy */
    ble_svc_gap_init();  // Standardowy GAP
    ble_svc_gatt_init(); // Standardowy GATT
    
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    /* Konfiguracja callbacka synchronizacji */
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    /* Uruchomienie zadania NimBLE */
    nimble_port_freertos_init(nimble_host_task);

    /* Uruchomienie zadania symulacyjnego aplikacji */
    xTaskCreate(simulation_task, "sim_task", 2048, NULL, 10, NULL);
}