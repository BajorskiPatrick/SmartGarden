#include "veml7700.h"
#include "esp_log.h"
#include <math.h>
#include <string.h> // dla memset

static const char *TAG = "VEML7700";

#define VEML7700_CORR_C4  6.0135e-13
#define VEML7700_CORR_C3 -9.3924e-9
#define VEML7700_CORR_C2  8.1488e-5
#define VEML7700_CORR_C1  1.0023

#define REG_ALS_CONF_0 0x00
#define REG_ALS_WH     0x01
#define REG_ALS_WL     0x02
#define REG_POWER_SAV  0x03
#define REG_ALS        0x04
#define REG_WHITE      0x05
#define REG_ALS_INT    0x06
#define REG_ID         0x07

#define VEML7700_DEVICE_ID 0x81

// Helper: Makra do obsługi mutexów (standard w i2cdev)
#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

static esp_err_t write_register(veml7700_handle_t *handle, uint8_t reg, uint16_t value) {
    // VEML7700 to Little Endian (LSB first)
    uint8_t data[2] = { (uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF) };
    
    I2C_DEV_TAKE_MUTEX(&handle->i2c_dev);
    esp_err_t err = i2c_dev_write_reg(&handle->i2c_dev, reg, data, 2);
    I2C_DEV_GIVE_MUTEX(&handle->i2c_dev);
    return err;
}

static esp_err_t read_register(veml7700_handle_t *handle, uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    
    I2C_DEV_TAKE_MUTEX(&handle->i2c_dev);
    esp_err_t err = i2c_dev_read_reg(&handle->i2c_dev, reg, data, 2);
    I2C_DEV_GIVE_MUTEX(&handle->i2c_dev);
    
    if (err == ESP_OK) {
        *value = (data[1] << 8) | data[0];
    }
    return err;
}

static esp_err_t update_conf_register(veml7700_handle_t *handle) {
    uint16_t conf_val = 0;
    conf_val |= (handle->gain << 11);
    conf_val |= (handle->integration_time << 6);
    conf_val |= (handle->persistence << 4);
    conf_val |= ((handle->interrupt_enable ? 1 : 0) << 1);
    conf_val |= ((handle->shutdown ? 1 : 0) << 0);
    return write_register(handle, REG_ALS_CONF_0, conf_val);
}

// --- Public API ---

esp_err_t veml7700_init_desc(veml7700_handle_t *handle, i2c_port_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio) {
    CHECK_ARG(handle);
    
    // Konfiguracja struktury i2c_dev wewnątrz handle
    memset(&handle->i2c_dev, 0, sizeof(i2c_dev_t));
    handle->i2c_dev.port = port;
    handle->i2c_dev.addr = VEML7700_I2C_ADDR;
    handle->i2c_dev.cfg.sda_io_num = sda_gpio;
    handle->i2c_dev.cfg.scl_io_num = scl_gpio;
    
    // i2cdev automatycznie zarządza dzieleniem magistrali
    return i2c_dev_create_mutex(&handle->i2c_dev);
}

esp_err_t veml7700_init(veml7700_handle_t *handle) {
    CHECK_ARG(handle);

    esp_err_t err = veml7700_read_id(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to verify VEML7700 ID");
        return err;
    }

    handle->gain = VEML7700_GAIN_1_8;
    handle->integration_time = VEML7700_IT_100MS;
    handle->persistence = VEML7700_PERS_1;
    handle->interrupt_enable = false;
    handle->shutdown = false;

    return update_conf_register(handle);
}

esp_err_t veml7700_read_id(veml7700_handle_t *handle) {
    uint16_t id_val;
    CHECK(read_register(handle, REG_ID, &id_val));

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
    return update_conf_register(handle);
}

esp_err_t veml7700_set_shutdown(veml7700_handle_t *handle, bool shutdown) {
    handle->shutdown = shutdown;
    return update_conf_register(handle);
}

esp_err_t veml7700_set_power_saving(veml7700_handle_t *handle, bool enable, veml7700_psm_mode_t mode) {
    uint16_t psm_val = 0;
    psm_val |= (mode << 1);
    psm_val |= (enable ? 1 : 0);
    return write_register(handle, REG_POWER_SAV, psm_val);
}

esp_err_t veml7700_set_interrupts(veml7700_handle_t *handle, bool enable, uint16_t high_threshold, uint16_t low_threshold) {
    CHECK(write_register(handle, REG_ALS_WH, high_threshold));
    CHECK(write_register(handle, REG_ALS_WL, low_threshold));
    handle->interrupt_enable = enable;
    return update_conf_register(handle);
}

esp_err_t veml7700_get_interrupt_status(veml7700_handle_t *handle, veml7700_interrupt_status_t *status) {
    uint16_t val;
    CHECK(read_register(handle, REG_ALS_INT, &val));
    status->was_low_threshold = (val & (1 << 15)) ? true : false;
    status->was_high_threshold = (val & (1 << 14)) ? true : false;
    return ESP_OK;
}

esp_err_t veml7700_read_als_raw(veml7700_handle_t *handle, uint16_t *raw_als) {
    return read_register(handle, REG_ALS, raw_als);
}

esp_err_t veml7700_read_white_raw(veml7700_handle_t *handle, uint16_t *raw_white) {
    return read_register(handle, REG_WHITE, raw_white);
}

static double get_resolution(veml7700_gain_t gain, veml7700_it_t it) {
    double gain_factor = 1.0;
    switch(gain) {
        case VEML7700_GAIN_2:   gain_factor = 2.0;   break;
        case VEML7700_GAIN_1:   gain_factor = 1.0;   break;
        case VEML7700_GAIN_1_4: gain_factor = 0.25;  break;
        case VEML7700_GAIN_1_8: gain_factor = 0.125; break;
    }
    double it_ms = 100.0;
    switch(it) {
        case VEML7700_IT_25MS:  it_ms = 25.0;  break;
        case VEML7700_IT_50MS:  it_ms = 50.0;  break;
        case VEML7700_IT_100MS: it_ms = 100.0; break;
        case VEML7700_IT_200MS: it_ms = 200.0; break;
        case VEML7700_IT_400MS: it_ms = 400.0; break;
        case VEML7700_IT_800MS: it_ms = 800.0; break;
    }
    return 0.0042 * (800.0 / it_ms) * (2.0 / gain_factor);
}

static veml7700_gain_t get_auto_gain_for_raw(veml7700_gain_t current_gain, uint16_t raw) {
    veml7700_gain_t new_gain = current_gain;

    // Progi histerezy dla auto-gain:
    // Dolny: < 100 zliczeń (zbyt mała precyzja)
    // Górny: > 10000 zliczeń (ryzyko nasycenia)
    if (raw > 10000) {
        // Zmniejszamy czułość (kolejność: x2 -> x1 -> x1/4 -> x1/8)
        if (current_gain == VEML7700_GAIN_2) new_gain = VEML7700_GAIN_1;
        else if (current_gain == VEML7700_GAIN_1) new_gain = VEML7700_GAIN_1_4;
        else if (current_gain == VEML7700_GAIN_1_4) new_gain = VEML7700_GAIN_1_8;
    } else if (raw < 100) {
        // Zwiększamy czułość (kolejność: x1/8 -> x1/4 -> x1 -> x2)
        if (current_gain == VEML7700_GAIN_1_8) new_gain = VEML7700_GAIN_1_4;
        else if (current_gain == VEML7700_GAIN_1_4) new_gain = VEML7700_GAIN_1;
        else if (current_gain == VEML7700_GAIN_1) new_gain = VEML7700_GAIN_2;
    }

    return new_gain;
}

esp_err_t veml7700_convert_als_raw_to_lux(uint16_t raw, veml7700_gain_t gain, veml7700_it_t it, double *lux) {
    if (lux == NULL) return ESP_ERR_INVALID_ARG;

    // Obliczenie podstawowej rozdzielczości (Gain/IT)
    double resolution = get_resolution(gain, it);

    // Wstępne obliczenie luksów
    double lux_linear = raw * resolution;

    // Korekcja nieliniowości
    double lux_corrected = (VEML7700_CORR_C4 * pow(lux_linear, 4)) +
                           (VEML7700_CORR_C3 * pow(lux_linear, 3)) +
                           (VEML7700_CORR_C2 * pow(lux_linear, 2)) +
                           (VEML7700_CORR_C1 * lux_linear);

    *lux = lux_corrected;
    return ESP_OK;
}

esp_err_t veml7700_read_lux(veml7700_handle_t *handle, double *lux) {
    if (handle == NULL || lux == NULL) return ESP_ERR_INVALID_ARG;

    // Snapshot konfiguracji użytej do tego pomiaru (ważne: po auto-gain zmieniamy config na przyszłość)
    veml7700_gain_t measurement_gain = handle->gain;
    veml7700_it_t measurement_it = handle->integration_time;
    veml7700_pers_t measurement_pers = handle->persistence;

    uint16_t raw;
    esp_err_t err = veml7700_read_als_raw(handle, &raw);
    if (err != ESP_OK) return err;

    // Jeżeli RAW jest poza zakresem, ustawiamy gain na przyszłość, ale NIE robimy ponownego odczytu.
    veml7700_gain_t new_gain = get_auto_gain_for_raw(measurement_gain, raw);
    if (new_gain != measurement_gain) {
        esp_err_t cfg_err = veml7700_set_config(handle, new_gain, measurement_it, measurement_pers);
        if (cfg_err == ESP_OK) {
            ESP_LOGW(TAG,
                     "Lux measurement uncertain (raw=%u out of range); adjusted gain for next read (%d -> %d). Returning converted value from current raw.",
                     (unsigned)raw, (int)measurement_gain, (int)new_gain);
        } else {
            ESP_LOGW(TAG,
                     "Lux measurement uncertain (raw=%u out of range); failed to adjust gain (%d -> %d): %s. Returning converted value from current raw.",
                     (unsigned)raw, (int)measurement_gain, (int)new_gain, esp_err_to_name(cfg_err));
        }
    } else if (raw > 10000 || raw < 100) {
        // Poza zakresem, ale gain już na granicy
        ESP_LOGW(TAG,
                 "Lux measurement uncertain (raw=%u out of range); gain already at limit (%d). Returning converted value from current raw.",
                 (unsigned)raw, (int)measurement_gain);
    }

    return veml7700_convert_als_raw_to_lux(raw, measurement_gain, measurement_it, lux);
}

esp_err_t veml7700_auto_adjust_gain(veml7700_handle_t *handle) {
    uint16_t raw;
    esp_err_t err = veml7700_read_als_raw(handle, &raw);
    if (err != ESP_OK) return err;

    // Progi histerezy dla auto-gain:
    // Dolny: < 100 zliczeń (zbyt mała precyzja)
    // Górny: > 10000 zliczeń (ryzyko nasycenia)
    
    veml7700_gain_t current_gain = handle->gain;
    veml7700_gain_t new_gain = get_auto_gain_for_raw(current_gain, raw);

    if (new_gain != current_gain) {
        // Aplikuj zmianę zachowując pozostałe parametry (IT, Persistence)
        return veml7700_set_config(handle, new_gain, handle->integration_time, handle->persistence);
    }

    return ESP_OK;
}