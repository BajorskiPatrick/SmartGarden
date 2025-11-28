#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

/* NimBLE Includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#define GATTC_TAG "GATTC_NIMBLE"

/* DOCELOWY ADRES MAC (Twój iTAG) */
static const uint8_t target_mac[6] = {0xFF, 0xFF, 0x1B, 0x0A, 0xF9, 0x96};

/* --- DEFINICJE UUID --- */
#define UUID_SVC_BATTERY      0x180F
#define UUID_CHAR_BATTERY     0x2A19

#define UUID_SVC_ALERT        0x1802
#define UUID_CHAR_ALERT       0x2A06

#define UUID_SVC_UNKNOWN      0xFFE0
#define UUID_CHAR_UNKNOWN     0xFFE1

/* Zmienne globalne stanu */
static bool is_connected = false;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* Uchwyty do charakterystyk (value handles) - potrzebne do identyfikacji źródła powiadomienia */
static uint16_t h_char_batt_val = 0;
static uint16_t h_char_alert_val = 0;
static uint16_t h_char_unk_val  = 0;

/* Logika aplikacji */
static uint8_t click_count = 0;
static bool trigger_alarm = false;

/* Deklaracje funkcji */
void ble_app_scan(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);

/**
 * Callback po znalezieniu deskryptora.
 * Jeśli to CCCD (0x2902), zapisujemy do niego 0x01,0x00, żeby włączyć powiadomienia.
 */
static int on_disc_dsc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                       void *arg)
{
    if (error != NULL) {
        return 0; // Koniec lub błąd
    }

    /* Sprawdzamy czy to Client Characteristic Configuration Descriptor (0x2902) */
    if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
        ESP_LOGI(GATTC_TAG, "Found CCCD (0x2902) for char handle %d. Subscribing...", chr_val_handle);
        
        uint8_t value[2] = {1, 0}; // Bit 1 = Notify
        int rc = ble_gattc_write_flat(conn_handle, dsc->handle, value, sizeof(value), NULL, NULL);
        if (rc != 0) {
            ESP_LOGE(GATTC_TAG, "Failed to subscribe; rc=%d", rc);
        }
    }
    return 0;
}

/**
 * Callback po znalezieniu charakterystyk.
 * Zapisuje uchwyty i uruchamia szukanie deskryptorów (w celu subskrypcji).
 */
static int on_disc_char(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr,
                        void *arg)
{
    if (error != NULL) {
        return 0;
    }

    uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
    uint16_t end_handle = (uint16_t)((uintptr_t)arg); // Koniec serwisu przekazany w argumencie

    if (uuid16 == UUID_CHAR_BATTERY) {
        ESP_LOGI(GATTC_TAG, "Found BATTERY Char (2A19)");
        h_char_batt_val = chr->val_handle;
        
        // Odczyt jednorazowy
        ble_gattc_read(conn_handle, h_char_batt_val, NULL, NULL);
        
        // Szukaj deskryptorów, żeby włączyć Notify
        ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, end_handle, on_disc_dsc, NULL);
    }
    else if (uuid16 == UUID_CHAR_ALERT) {
        ESP_LOGI(GATTC_TAG, "Found ALERT Char (2A06)");
        h_char_alert_val = chr->val_handle;
        
        // Alert zazwyczaj ma Notify i Write
        ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, end_handle, on_disc_dsc, NULL);
    }
    else if (uuid16 == UUID_CHAR_UNKNOWN) {
        ESP_LOGI(GATTC_TAG, "Found UNKNOWN Char (FFE1)");
        h_char_unk_val = chr->val_handle;

        // Odczyt jednorazowy
        ble_gattc_read(conn_handle, h_char_unk_val, NULL, NULL);

        // Szukaj deskryptorów
        ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, end_handle, on_disc_dsc, NULL);
    }

    return 0;
}

/**
 * Callback po znalezieniu serwisów.
 * Uruchamia szukanie charakterystyk wewnątrz zakresu serwisu.
 */
static int on_disc_service(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *service,
                           void *arg)
{
    if (error != NULL) {
        return 0;
    }

    uint16_t uuid16 = ble_uuid_u16(&service->uuid.u);

    if (uuid16 == UUID_SVC_BATTERY || uuid16 == UUID_SVC_ALERT || uuid16 == UUID_SVC_UNKNOWN) {
        ESP_LOGI(GATTC_TAG, "Service found: 0x%04x. Searching attributes...", uuid16);
        
        // Przekazujemy service->end_handle jako argument, żeby wiedzieć gdzie kończyć szukanie deskryptorów
        ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle, 
                                on_disc_char, (void*)((uintptr_t)service->end_handle));
    }

    return 0;
}

/**
 * Główny callback zdarzeń GAP
 */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    int rc;

    switch (event->type) {
    /* --- ODBIERANIE DANYCH (NOTIFY) --- */
    case BLE_GAP_EVENT_NOTIFY_RX: {
        /* To zdarzenie jest wywoływane, gdy przychodzi Notify/Indicate */
        uint16_t handle = event->notify_rx.attr_handle;
        uint8_t data_buf[10];
        uint16_t data_len;

        /* Wyciągnij dane z mbuf do bufora płaskiego */
        rc = ble_hs_mbuf_to_flat(event->notify_rx.om, data_buf, sizeof(data_buf), &data_len);
        if (rc != 0 || data_len == 0) return 0;

        if (handle == h_char_batt_val) {
            ESP_LOGI(GATTC_TAG, ">>> [BATERIA] Poziom: %d %%", data_buf[0]);
        } 
        else if (handle == h_char_alert_val) {
            ESP_LOGI(GATTC_TAG, ">>> [ALERT] Zmiana stanu! (Hex: %02x)", data_buf[0]);
        } 
        else if (handle == h_char_unk_val) {
            ESP_LOGI(GATTC_TAG, ">>> [PRZYCISK] Otrzymano sygnał: %02x", data_buf[0]);

            // Logika "5 kliknięć"
            if (data_buf[0] == 0x01) {
                click_count++;
                ESP_LOGI(GATTC_TAG, "Licznik kliknięć: %d / 5", click_count);

                if (click_count >= 5) {
                    ESP_LOGW(GATTC_TAG, "!!! 5 KLIKNIĘĆ OSIĄGNIĘTE - ZLECENIE ALARMU !!!");
                    click_count = 0;
                    trigger_alarm = true;
                }
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISC: {
        if (memcmp(event->disc.addr.val, target_mac, 6) == 0) {
            ESP_LOGI(GATTC_TAG, ">>> ZNALEZIONO iTAG PO ADRESIE MAC! <<<");
            
            ble_gap_disc_cancel();

            rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr, 30000, NULL, ble_gap_event, NULL);
            if (rc != 0) {
                ESP_LOGE(GATTC_TAG, "Failed to connect; rc=%d", rc);
                ble_app_scan(); 
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(GATTC_TAG, "Connected. Conn Handle: %d", event->connect.conn_handle);
            conn_handle = event->connect.conn_handle;
            is_connected = true;

            // Start discovery
            ble_gattc_disc_all_svcs(conn_handle, on_disc_service, NULL);

        } else {
            ESP_LOGE(GATTC_TAG, "Connection failed; status=%d", event->connect.status);
            ble_app_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(GATTC_TAG, "Disconnected. Reason: %d", event->disconnect.reason);
        is_connected = false;
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        
        // Reset uchwytów
        h_char_batt_val = 0;
        h_char_alert_val = 0;
        h_char_unk_val = 0;

        ESP_LOGI(GATTC_TAG, "Restarting scanning...");
        ble_app_scan();
        return 0;
    
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (!is_connected) {
             ble_app_scan();
        }
        return 0;

    default:
        return 0;
    }
}

void ble_app_scan(void)
{
    struct ble_gap_disc_params disc_params;

    disc_params.filter_duplicates = 0;
    disc_params.passive = 0;
    disc_params.itvl = 0x50;
    disc_params.window = 0x30;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 30000, &disc_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(GATTC_TAG, "Error initiating GAP discovery; rc=%d", rc);
    } else {
        ESP_LOGI(GATTC_TAG, "Scanning started...");
    }
}

void ble_app_on_sync(void)
{
    ESP_LOGI(GATTC_TAG, "NimBLE Host Synced");
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    ble_app_scan();
}

void nimble_host_task(void *param)
{
    ESP_LOGI(GATTC_TAG, "NimBLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nimble_port_init());
    
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(GATTC_TAG, "Start pętli głównej. Czekam na 5 kliknięć...");

    while (1) {
        if (trigger_alarm) {
            if (is_connected && h_char_alert_val != 0) {
                
                // 1. WŁĄCZ ALARM (High Alert = 0x02)
                uint8_t write_val = 0x02;
                ESP_LOGW(GATTC_TAG, ">>> ALARM START! Typ: 0x%02x (Trwa 5s) <<<", write_val);
                
                ble_gattc_write_no_rsp_flat(conn_handle, h_char_alert_val, &write_val, sizeof(write_val));

                // 2. CZEKAJ 5 SEKUND
                vTaskDelay(5000 / portTICK_PERIOD_MS);

                // 3. WYŁĄCZ ALARM
                ESP_LOGI(GATTC_TAG, ">>> ALARM STOP (Wysylanie 0x00) <<<");
                write_val = 0x00;
                
                ble_gattc_write_no_rsp_flat(conn_handle, h_char_alert_val, &write_val, sizeof(write_val));

                trigger_alarm = false;

            } else {
                ESP_LOGW(GATTC_TAG, "Nie można włączyć alarmu - brak połączenia lub brak uchwytu char.");
                trigger_alarm = false;
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}