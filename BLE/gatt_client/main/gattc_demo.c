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
// W NimBLE adresy podajemy w odwrotnej kolejności bajtów w strukturze, 
// ale tutaj zdefiniujemy go tak jak w Twoim kodzie i użyjemy helpera do porównania.
static const uint8_t target_mac[6] = {0xFF, 0xFF, 0x1B, 0x0A, 0xF9, 0x96};

/* --- DEFINICJE UUID (NimBLE format) --- */
// 1. Battery Service
#define UUID_SVC_BATTERY      0x180F
#define UUID_CHAR_BATTERY     0x2A19

// 2. Immediate Alert Service
#define UUID_SVC_ALERT        0x1802
#define UUID_CHAR_ALERT       0x2A06

// 3. Unknown Service (Przyciski / Custom)
#define UUID_SVC_UNKNOWN      0xFFE0
#define UUID_CHAR_UNKNOWN     0xFFE1

/* Zmienne globalne stanu */
static bool is_connected = false;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* Uchwyty do charakterystyk (value handles) */
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
 * Callback obsługujący przychodzące powiadomienia (Notify/Indicate)
 */
static int on_incoming_notify(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr,
                              void *arg)
{
    if (attr == NULL) {
        return 0; // Zakończono subskrypcję lub błąd
    }

    uint8_t *val = attr->om->om_data;
    // int len = attr->om->om_len; // Jeśli potrzebujesz długości

    if (attr->handle == h_char_batt_val) {
        ESP_LOGI(GATTC_TAG, ">>> [BATERIA] Poziom: %d %%", val[0]);
    } 
    else if (attr->handle == h_char_alert_val) {
        ESP_LOGI(GATTC_TAG, ">>> [ALERT] Zmiana stanu! (Hex: %02x)", val[0]);
    } 
    else if (attr->handle == h_char_unk_val) {
        ESP_LOGI(GATTC_TAG, ">>> [PRZYCISK] Otrzymano sygnał: %02x", val[0]);

        // Zakładamy, że 0x01 to kliknięcie (single click)
        if (val[0] == 0x01) {
            click_count++;
            ESP_LOGI(GATTC_TAG, "Licznik kliknięć: %d / 5", click_count);

            if (click_count >= 5) {
                ESP_LOGW(GATTC_TAG, "!!! 5 KLIKNIĘĆ OSIĄGNIĘTE - ZLECENIE ALARMU !!!");
                click_count = 0;
                trigger_alarm = true; // Flaga dla pętli w app_main
            }
        }
    }

    return 0;
}

/**
 * Callback po zakończeniu discovery charakterystyk
 */
static int on_disc_char(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr,
                        void *arg)
{
    if (error != NULL) {
        // Błąd lub koniec iteracji (status 0 oznacza koniec, błąd to np. BLE_HS_EDONE)
        return (error->status == BLE_HS_EDONE) ? 0 : error->status;
    }

    /* Sprawdzamy UUID znalezionej charakterystyki */
    uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);

    if (uuid16 == UUID_CHAR_BATTERY) {
        ESP_LOGI(GATTC_TAG, "Found BATTERY Char (2A19)");
        h_char_batt_val = chr->val_handle;
        
        // Odczyt
        ble_gattc_read(conn_handle, h_char_batt_val, NULL, NULL);
        // Subskrypcja (Notify) - NimBLE ma helpera, który sam robi write do CCCD
        ble_gattc_subscribe(conn_handle, h_char_batt_val, on_incoming_notify, NULL);
    }
    else if (uuid16 == UUID_CHAR_ALERT) {
        ESP_LOGI(GATTC_TAG, "Found ALERT Char (2A06)");
        h_char_alert_val = chr->val_handle;
        
        // Alert zazwyczaj nie ma Read, tylko Write/Notify
        ble_gattc_subscribe(conn_handle, h_char_alert_val, on_incoming_notify, NULL);
    }
    else if (uuid16 == UUID_CHAR_UNKNOWN) {
        ESP_LOGI(GATTC_TAG, "Found UNKNOWN Char (FFE1)");
        h_char_unk_val = chr->val_handle;

        // Odczyt
        ble_gattc_read(conn_handle, h_char_unk_val, NULL, NULL);
        // Subskrypcja
        ble_gattc_subscribe(conn_handle, h_char_unk_val, on_incoming_notify, NULL);
    }

    return 0;
}

/**
 * Callback po zakończeniu discovery serwisów
 */
static int on_disc_service(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *service,
                           void *arg)
{
    if (error != NULL) {
        return (error->status == BLE_HS_EDONE) ? 0 : error->status;
    }

    uint16_t uuid16 = ble_uuid_u16(&service->uuid.u);

    // Jeśli znaleziono jeden z interesujących serwisów, szukamy w nim charakterystyk
    if (uuid16 == UUID_SVC_BATTERY || uuid16 == UUID_SVC_ALERT || uuid16 == UUID_SVC_UNKNOWN) {
        ESP_LOGI(GATTC_TAG, "Service found: 0x%04x. Searching attributes...", uuid16);
        ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle, on_disc_char, NULL);
    }

    return 0;
}

/**
 * Główny callback zdarzeń GAP (połączenie, rozłączenie, skanowanie)
 */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        /* Wynik skanowania */
        // Sprawdź czy to nasz MAC (NimBLE przechowuje MAC w event->disc.addr.val)
        // Uwaga: W NimBLE adres może być Public lub Random. 
        // Twój kod w Bluedroid zakładał Public (BLE_ADDR_TYPE_PUBLIC).
        
        // Sprawdźmy MAC
        if (memcmp(event->disc.addr.val, target_mac, 6) == 0) {
            ESP_LOGI(GATTC_TAG, ">>> ZNALEZIONO iTAG PO ADRESIE MAC! <<<");
            
            // Zatrzymaj skanowanie (ważne w NimBLE przed połączeniem, chyba że używamy automatu)
            ble_gap_disc_cancel();

            // Połącz
            rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr, 30000, NULL, ble_gap_event, NULL);
            if (rc != 0) {
                ESP_LOGE(GATTC_TAG, "Failed to connect to device; rc=%d", rc);
                // Wznów skanowanie jeśli fail
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

            // Rozpocznij szukanie wszystkich serwisów
            // (W NimBLE można od razu szukać po UUID, ale zrobimy 'disc_all' by zachować logikę podobną do oryginału)
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
        // Skanowanie zakończone (np. timeout). Jeśli nie połączeni, wznawiamy.
        ESP_LOGI(GATTC_TAG, "Scan complete/stopped.");
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

    /* Konfiguracja skanowania pasywna lub aktywna */
    disc_params.filter_duplicates = 0; // BLE_SCAN_DUPLICATE_DISABLE
    disc_params.passive = 0;           // 0 = Active Scan (jak w oryginale BLE_SCAN_TYPE_ACTIVE)
    disc_params.itvl = 0x50;           // 80 * 0.625ms (jak w oryginale)
    disc_params.window = 0x30;         // 48 * 0.625ms (jak w oryginale)
    disc_params.filter_policy = 0;     // BLE_SCAN_FILTER_ALLOW_ALL
    disc_params.limited = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 30000, &disc_params, ble_gap_event, NULL); // 30s timeout
    if (rc != 0) {
        ESP_LOGE(GATTC_TAG, "Error initiating GAP discovery; rc=%d", rc);
    } else {
        ESP_LOGI(GATTC_TAG, "Scanning started...");
    }
}

/* Callback inicjalizujący hosta (wywoływany po synchronizacji) */
void ble_app_on_sync(void)
{
    ESP_LOGI(GATTC_TAG, "NimBLE Host Synced");
    
    /* Upewnij się, że mamy adres */
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Rozpocznij skanowanie */
    ble_app_scan();
}

/* Zadanie FreeRTOS dla Hosta NimBLE */
void nimble_host_task(void *param)
{
    ESP_LOGI(GATTC_TAG, "NimBLE Host Task Started");
    /* Ta funkcja blokuje i obsługuje zdarzenia stosu */
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
    
    /* Konfiguracja callbacka synchronizacji */
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    /* Uruchomienie zadania NimBLE */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(GATTC_TAG, "Start pętli głównej. Czekam na 5 kliknięć...");

    /* Pętla główna (replika logiki z app_main w oryginale) */
    while (1) {
        if (trigger_alarm) {
            // Upewniamy się, że nadal jesteśmy połączeni i mamy uchwyt Alertu
            if (is_connected && h_char_alert_val != 0) {
                
                // 1. WŁĄCZ ALARM (High Alert = 0x02)
                uint8_t write_val = 0x02;
                ESP_LOGW(GATTC_TAG, ">>> ALARM START! Typ: 0x%02x (Trwa 5s) <<<", write_val);
                
                // Write Command (No Response) - ble_gattc_write_no_rsp_flat
                ble_gattc_write_no_rsp_flat(conn_handle, h_char_alert_val, &write_val, sizeof(write_val));

                // 2. CZEKAJ 5 SEKUND
                vTaskDelay(5000 / portTICK_PERIOD_MS);

                // 3. WYŁĄCZ ALARM
                ESP_LOGI(GATTC_TAG, ">>> ALARM STOP (Wysylanie 0x00) <<<");
                write_val = 0x00; // No Alert
                
                ble_gattc_write_no_rsp_flat(conn_handle, h_char_alert_val, &write_val, sizeof(write_val));

                // Reset flagi
                trigger_alarm = false;

            } else {
                ESP_LOGW(GATTC_TAG, "Nie można włączyć alarmu - brak połączenia lub brak uchwytu char.");
                trigger_alarm = false;
            }
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}