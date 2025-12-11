#include "veml7700.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "VEML7700";

// Adresy rejestrów 
#define REG_ALS_CONF_0 0x00
#define REG_ALS_WH     0x01
#define REG_ALS_WL     0x02
#define REG_POWER_SAV  0x03
#define REG_ALS        0x04
#define REG_WHITE      0x05
#define REG_ALS_INT    0x06
#define REG_ID         0x07

// Funkcja pomocnicza do zapisu rejestru 16-bitowego (Little Endian)
static esp_err_t write_register(veml7700_handle_t *handle, uint8_t reg, uint16_t value) {
    uint8_t data[3];
    data[0] = reg;
    data[1] = (uint8_t)(value & 0xFF);        // LSB
    data[2] = (uint8_t)((value >> 8) & 0xFF); // MSB

    // Zapis zgodnie z Rys. 3 dokumentacji 
    return i2c_master_write_to_device(handle->i2c_port, VEML7700_I2C_ADDR, data, 3, 1000 / portTICK_PERIOD_MS);
}

// Funkcja pomocnicza do odczytu rejestru 16-bitowego (Little Endian)
static esp_err_t read_register(veml7700_handle_t *handle, uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    // Zapis adresu rejestru, potem restart i odczyt danych (Rys. 4) 
    esp_err_t err = i2c_master_write_read_device(handle->i2c_port, VEML7700_I2C_ADDR, &reg, 1, data, 2, 1000 / portTICK_PERIOD_MS);
    if (err == ESP_OK) {
        *value = (data[1] << 8) | data[0]; // Składanie Little Endian
    }
    return err;
}

esp_err_t veml7700_init(veml7700_handle_t *handle, int port) {
    handle->i2c_port = port;
    // Domyślna konfiguracja: Gain x1/8, IT 100ms (bezpieczne startowe)
    // Musimy włączyć urządzenie (ALS_SD = 0) 
    return veml7700_set_config(handle, VEML7700_GAIN_1_8, VEML7700_IT_100MS);
}

esp_err_t veml7700_set_config(veml7700_handle_t *handle, veml7700_gain_t gain, veml7700_it_t it) {
    handle->gain = gain;
    handle->integration_time = it;

    uint16_t conf_val = 0;
    conf_val |= (gain << 11);       // Bity 12:11 
    conf_val |= (it << 6);          // Bity 9:6 
    conf_val |= (0 << 0);           // ALS_SD = 0 (Power ON)

    return write_register(handle, REG_ALS_CONF_0, conf_val);
}

esp_err_t veml7700_set_power_saving(veml7700_handle_t *handle, bool enable, veml7700_psm_mode_t mode) {
    uint16_t psm_val = 0;
    psm_val |= (mode << 1);        // Bity 2:1 
    psm_val |= (enable ? 1 : 0);   // Bit 0 (PSM_EN)

    return write_register(handle, REG_POWER_SAV, psm_val);
}

esp_err_t veml7700_set_interrupts(veml7700_handle_t *handle, bool enable, uint16_t high_threshold, uint16_t low_threshold) {
    esp_err_t err = write_register(handle, REG_ALS_WH, high_threshold);
    if (err != ESP_OK) return err;
    
    err = write_register(handle, REG_ALS_WL, low_threshold);
    if (err != ESP_OK) return err;

    // Aby włączyć przerwania, musimy zmodyfikować rejestr CONF_0 (bit 1) 
    // Pobieramy obecną konfigurację, aby nie nadpisać Gain/IT
    // Ponieważ nie przechowujemy pełnego stanu rejestru, odtwarzamy go ze struktury
    uint16_t conf_val = 0;
    conf_val |= (handle->gain << 11);
    conf_val |= (handle->integration_time << 6);
    conf_val |= (enable ? (1 << 1) : 0); // ALS_INT_EN

    return write_register(handle, REG_ALS_CONF_0, conf_val);
}

esp_err_t veml7700_read_als_raw(veml7700_handle_t *handle, uint16_t *raw_als) {
    return read_register(handle, REG_ALS, raw_als);
}

esp_err_t veml7700_read_white_raw(veml7700_handle_t *handle, uint16_t *raw_white) {
    return read_register(handle, REG_WHITE, raw_white);
}

// Funkcja obliczająca rozdzielczość na podstawie tabeli 9 
static double get_resolution(veml7700_gain_t gain, veml7700_it_t it) {
    // Bazowa rozdzielczość dla Gain x2 i IT 800ms wynosi 0.0042 
    // Formuła: Res = 0.0042 * (800 / IT_ms) * (2 / Gain_factor)
    
    double gain_factor = 1.0;
    switch(gain) {
        case VEML7700_GAIN_1:   gain_factor = 1.0; break;
        case VEML7700_GAIN_2:   gain_factor = 2.0; break;
        case VEML7700_GAIN_1_4: gain_factor = 0.25; break;
        case VEML7700_GAIN_1_8: gain_factor = 0.125; break;
    }

    double it_ms = 100.0;
    switch(it) {
        case VEML7700_IT_25MS:  it_ms = 25.0; break;
        case VEML7700_IT_50MS:  it_ms = 50.0; break;
        case VEML7700_IT_100MS: it_ms = 100.0; break;
        case VEML7700_IT_200MS: it_ms = 200.0; break;
        case VEML7700_IT_400MS: it_ms = 400.0; break;
        case VEML7700_IT_800MS: it_ms = 800.0; break;
    }

    // Obliczenie na podstawie bazy (Gain x2, IT 800ms -> 0.0042)
    // Jeśli skrócimy czas 2x -> rozdzielczość rośnie (wartość numeryczna x2)
    // Jeśli zmniejszymy Gain 2x -> rozdzielczość rośnie (wartość numeryczna x2)
    return 0.0042 * (800.0 / it_ms) * (2.0 / gain_factor);
}

esp_err_t veml7700_read_lux(veml7700_handle_t *handle, double *lux) {
    uint16_t raw;
    esp_err_t err = veml7700_read_als_raw(handle, &raw);
    if (err != ESP_OK) return err;

    double resolution = get_resolution(handle->gain, handle->integration_time);
    
    // Korekcja nieliniowości dla wysokich wartości (opcjonalna, ale dobra dla "full library")
    // Tutaj zastosujemy proste przeliczenie liniowe zgodne z podstawową dokumentacją:
    *lux = raw * resolution;
    
    return ESP_OK;
}

esp_err_t veml7700_auto_adjust_gain(veml7700_handle_t *handle) {
    uint16_t raw;
    esp_err_t err = veml7700_read_als_raw(handle, &raw);
    if (err != ESP_OK) return err;

    // Jeśli wartość jest zbyt niska (< 100), zwiększ czułość (Gain)
    if (raw < 100 && handle->gain != VEML7700_GAIN_2) {
        return veml7700_set_config(handle, VEML7700_GAIN_2, handle->integration_time);
    }
    // Jeśli nasycenie (> 10000), zmniejsz czułość
    else if (raw > 10000 && handle->gain != VEML7700_GAIN_1_8) {
         return veml7700_set_config(handle, VEML7700_GAIN_1_8, handle->integration_time);
    }
    return ESP_OK;
}