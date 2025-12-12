#include "veml7700.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "VEML7700";

// Współczynniki korekcji nieliniowości z Noty Aplikacyjnej Vishay
// Używane do poprawy dokładności przy wysokim natężeniu światła.
#define VEML7700_CORR_C4  6.0135e-13
#define VEML7700_CORR_C3 -9.3924e-9
#define VEML7700_CORR_C2  8.1488e-5
#define VEML7700_CORR_C1  1.0023

// Adresy rejestrów [cite: 403]
#define REG_ALS_CONF_0 0x00
#define REG_ALS_WH     0x01
#define REG_ALS_WL     0x02
#define REG_POWER_SAV  0x03
#define REG_ALS        0x04
#define REG_WHITE      0x05
#define REG_ALS_INT    0x06
#define REG_ID         0x07

// Oczekiwany kod ID (LSB) 
#define VEML7700_DEVICE_ID 0x81

// --- Helper Functions ---

static esp_err_t write_register(veml7700_handle_t *handle, uint8_t reg, uint16_t value) {
    uint8_t data[3];
    data[0] = reg;
    data[1] = (uint8_t)(value & 0xFF);        // LSB
    data[2] = (uint8_t)((value >> 8) & 0xFF); // MSB
    // Zapis Word (Command Code + Data Low + Data High) [cite: 190]
    return i2c_master_write_to_device(handle->i2c_port, VEML7700_I2C_ADDR, data, 3, 1000 / portTICK_PERIOD_MS);
}

static esp_err_t read_register(veml7700_handle_t *handle, uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    // Zapis adresu rejestru (bez stopu), restart, odczyt 2 bajtów [cite: 241]
    esp_err_t err = i2c_master_write_read_device(handle->i2c_port, VEML7700_I2C_ADDR, &reg, 1, data, 2, 1000 / portTICK_PERIOD_MS);
    if (err == ESP_OK) {
        *value = (data[1] << 8) | data[0]; // Little Endian
    }
    return err;
}

// Funkcja wewnętrzna aktualizująca rejestr konfiguracyjny (0x00) na podstawie stanu struktury
static esp_err_t update_conf_register(veml7700_handle_t *handle) {
    uint16_t conf_val = 0;
    
    // Składanie bitów zgodnie z Tabelą 1 
    conf_val |= (handle->gain << 11);            // Bits 12:11
    conf_val |= (handle->integration_time << 6); // Bits 9:6
    conf_val |= (handle->persistence << 4);      // Bits 5:4 (ALS_PERS)
    conf_val |= ((handle->interrupt_enable ? 1 : 0) << 1); // Bit 1 (ALS_INT_EN)
    conf_val |= ((handle->shutdown ? 1 : 0) << 0);         // Bit 0 (ALS_SD)

    // Bity zarezerwowane: 15:13 (000), 10 (0), 3:2 (00) - są domyślnie 0 w conf_val
    return write_register(handle, REG_ALS_CONF_0, conf_val);
}

// --- Public API ---

esp_err_t veml7700_init(veml7700_handle_t *handle, i2c_port_t port) {
    handle->i2c_port = port;
    
    // Weryfikacja ID urządzenia przed konfiguracją
    esp_err_t err = veml7700_read_id(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify VEML7700 ID");
        return err;
    }

    // Domyślna konfiguracja "na start":
    // Gain x1/8, IT 100ms, Persistence 1, INT Disabled, Power ON (SD=0)
    handle->gain = VEML7700_GAIN_1_8;
    handle->integration_time = VEML7700_IT_100MS;
    handle->persistence = VEML7700_PERS_1;
    handle->interrupt_enable = false;
    handle->shutdown = false;

    // Aplikacja konfiguracji
    return update_conf_register(handle);
}

esp_err_t veml7700_read_id(veml7700_handle_t *handle) {
    uint16_t id_val;
    esp_err_t err = read_register(handle, REG_ID, &id_val);
    if (err != ESP_OK) return err;

    // Sprawdzenie młodszego bajtu (LSB) zgodnie z Tabelą 8 
    uint8_t device_id_code = (id_val & 0xFF);
    if (device_id_code != VEML7700_DEVICE_ID) {
        ESP_LOGE(TAG, "ID Mismatch. Expected: 0x%02X, Got: 0x%02X", VEML7700_DEVICE_ID, device_id_code);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "VEML7700 Found. ID: 0x%04X", id_val);
    return ESP_OK;
}

esp_err_t veml7700_set_config(veml7700_handle_t *handle, veml7700_gain_t gain, veml7700_it_t it, veml7700_pers_t pers) {
    handle->gain = gain;
    handle->integration_time = it;
    handle->persistence = pers;
    // Nie zmieniamy interrupt_enable ani shutdown, tylko parametry pomiarowe
    return update_conf_register(handle);
}

esp_err_t veml7700_set_shutdown(veml7700_handle_t *handle, bool shutdown) {
    handle->shutdown = shutdown;
    return update_conf_register(handle);
}

esp_err_t veml7700_set_power_saving(veml7700_handle_t *handle, bool enable, veml7700_psm_mode_t mode) {
    uint16_t psm_val = 0;
    // Tabela 4 [cite: 430]
    psm_val |= (mode << 1);        // Bity 2:1
    psm_val |= (enable ? 1 : 0);   // Bit 0 (PSM_EN)
    
    // Zapis do rejestru 0x03 (nie wpływa na CONF_0)
    return write_register(handle, REG_POWER_SAV, psm_val);
}

esp_err_t veml7700_set_interrupts(veml7700_handle_t *handle, bool enable, uint16_t high_threshold, uint16_t low_threshold) {
    esp_err_t err;

    // Najpierw ustawiamy progi (Rejestry 0x01, 0x02) [cite: 423, 427]
    err = write_register(handle, REG_ALS_WH, high_threshold);
    if (err != ESP_OK) return err;
    
    err = write_register(handle, REG_ALS_WL, low_threshold);
    if (err != ESP_OK) return err;

    // Aktualizujemy stan w strukturze i wysyłamy do CONF_0
    handle->interrupt_enable = enable;
    return update_conf_register(handle);
}

esp_err_t veml7700_get_interrupt_status(veml7700_handle_t *handle, veml7700_interrupt_status_t *status) {
    uint16_t val;
    esp_err_t err = read_register(handle, REG_ALS_INT, &val); // Rejestr 0x06 
    if (err != ESP_OK) return err;

    // Tabela 7: Bit 15 = Low TH exc, Bit 14 = High TH exc
    status->was_low_threshold = (val & (1 << 15)) ? true : false;
    status->was_high_threshold = (val & (1 << 14)) ? true : false;

    return ESP_OK;
}

esp_err_t veml7700_read_als_raw(veml7700_handle_t *handle, uint16_t *raw_als) {
    return read_register(handle, REG_ALS, raw_als); // Rejestr 0x04 [cite: 433]
}

esp_err_t veml7700_read_white_raw(veml7700_handle_t *handle, uint16_t *raw_white) {
    return read_register(handle, REG_WHITE, raw_white); // Rejestr 0x05 [cite: 440]
}

// Funkcja obliczająca rozdzielczość na podstawie Tabeli "Basic Characteristics" i wzoru
// Res = 0.0042 * (800 / IT) * (2 / Gain) [cite: 35, 469]
static double get_resolution(veml7700_gain_t gain, veml7700_it_t it) {
    double gain_factor = 1.0;
    // Mapowanie enum na mnożnik rzeczywisty
    switch(gain) {
        case VEML7700_GAIN_2:   gain_factor = 2.0;   break; // x2
        case VEML7700_GAIN_1:   gain_factor = 1.0;   break; // x1
        case VEML7700_GAIN_1_4: gain_factor = 0.25;  break; // x1/4
        case VEML7700_GAIN_1_8: gain_factor = 0.125; break; // x1/8
    }

    double it_ms = 100.0;
    // Mapowanie enum na czas w ms
    switch(it) {
        case VEML7700_IT_25MS:  it_ms = 25.0;  break;
        case VEML7700_IT_50MS:  it_ms = 50.0;  break;
        case VEML7700_IT_100MS: it_ms = 100.0; break;
        case VEML7700_IT_200MS: it_ms = 200.0; break;
        case VEML7700_IT_400MS: it_ms = 400.0; break;
        case VEML7700_IT_800MS: it_ms = 800.0; break;
    }

    // Formuła bazująca na wartości 0.0042 lx/step dla Gain x2 i IT 800ms
    return 0.0042 * (800.0 / it_ms) * (2.0 / gain_factor);
}

esp_err_t veml7700_read_lux(veml7700_handle_t *handle, double *lux) {
    uint16_t raw;
    esp_err_t err = veml7700_read_als_raw(handle, &raw);
    if (err != ESP_OK) return err;

    // 1. Obliczenie podstawowej rozdzielczości (Gain/IT)
    double resolution = get_resolution(handle->gain, handle->integration_time);
    
    // 2. Wstępne obliczenie luksów (model liniowy)
    double lux_linear = raw * resolution;

    // 3. Korekcja nieliniowości (zgodnie z App Note)
    // Sensor wykazuje nieliniowość przy wyższych wartościach strumienia świetlnego.
    // Wzór: Lux_Corr = C4*L^4 + C3*L^3 + C2*L^2 + C1*L
    // Gdzie L to lux_linear.
    
    double lux_corrected = (VEML7700_CORR_C4 * pow(lux_linear, 4)) +
                           (VEML7700_CORR_C3 * pow(lux_linear, 3)) +
                           (VEML7700_CORR_C2 * pow(lux_linear, 2)) +
                           (VEML7700_CORR_C1 * lux_linear);

    *lux = lux_corrected;
    return ESP_OK;
}

esp_err_t veml7700_auto_adjust_gain(veml7700_handle_t *handle) {
    uint16_t raw;
    esp_err_t err = veml7700_read_als_raw(handle, &raw);
    if (err != ESP_OK) return err;

    // Progi histerezy dla auto-gain:
    // Dolny: < 100 zliczeń (zbyt mała precyzja)
    // Górny: > 10000 zliczeń (ryzyko nasycenia)
    
    veml7700_gain_t current_gain = handle->gain;
    veml7700_gain_t new_gain = current_gain;

    if (raw > 10000) {
        // Zmniejszamy czułość (kolejność: x2 -> x1 -> x1/4 -> x1/8)
        if (current_gain == VEML7700_GAIN_2) new_gain = VEML7700_GAIN_1;
        else if (current_gain == VEML7700_GAIN_1) new_gain = VEML7700_GAIN_1_4;
        else if (current_gain == VEML7700_GAIN_1_4) new_gain = VEML7700_GAIN_1_8;
    } 
    else if (raw < 100) {
        // Zwiększamy czułość (kolejność: x1/8 -> x1/4 -> x1 -> x2)
        if (current_gain == VEML7700_GAIN_1_8) new_gain = VEML7700_GAIN_1_4;
        else if (current_gain == VEML7700_GAIN_1_4) new_gain = VEML7700_GAIN_1;
        else if (current_gain == VEML7700_GAIN_1) new_gain = VEML7700_GAIN_2;
    }

    if (new_gain != current_gain) {
        // Aplikuj zmianę zachowując pozostałe parametry (IT, Persistence)
        return veml7700_set_config(handle, new_gain, handle->integration_time, handle->persistence);
    }

    return ESP_OK;
}