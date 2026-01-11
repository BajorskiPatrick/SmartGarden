#include "sensors.h"
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "i2cdev.h"
#include "bmp280.h"
#include "veml7700.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h" // Dla esp_rom_delay_us
#include <sys/time.h>    // Dla gettimeofday

#include "mqtt_app.h"
#include "alert_limiter.h"

static const char *TAG = "SENSORS";

// --- KONFIGURACJA SPRZĘTOWA ---
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000

#define WATER_LEVEL_GPIO            GPIO_NUM_18

#define SOIL_ADC_CHANNEL            ADC_CHANNEL_6
#define SOIL_DRY_VAL                2800            
#define SOIL_WET_VAL                1200            

// Zmienne globalne modułu (statyczne)
static veml7700_handle_t veml_sensor;
static bmp280_t bme280_dev;
static adc_oneshot_unit_handle_t adc1_handle;

static bool s_has_veml7700 = false;
static bool s_has_bme280 = false;
static bool s_has_soil = true;
static bool s_has_water = true;

static bool s_prev_soil_ok = true;
static bool s_prev_bme_ok = true;
static bool s_prev_veml_ok = true;

static long map_val(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Funkcja resetująca magistralę I2C (uwalnia linię SDA jeśli slave ją trzyma)
static void i2c_bus_reset(void) {
    ESP_LOGI(TAG, "Wykonuję reset magistrali I2C...");
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << I2C_MASTER_SCL_IO) | (1ULL << I2C_MASTER_SDA_IO),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    
    // Generuj 9 taktów zegara, aby odblokować slave'a
    for (int i = 0; i < 9; i++) {
        gpio_set_level(I2C_MASTER_SCL_IO, 0);
        esp_rom_delay_us(10);
        gpio_set_level(I2C_MASTER_SCL_IO, 1);
        esp_rom_delay_us(10);
    }
    
    // Stop condition
    gpio_set_level(I2C_MASTER_SCL_IO, 0);
    esp_rom_delay_us(10);
    gpio_set_level(I2C_MASTER_SDA_IO, 0);
    esp_rom_delay_us(10);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    esp_rom_delay_us(10);
    gpio_set_level(I2C_MASTER_SDA_IO, 1);

    // Przywróć domyślny stan
    gpio_reset_pin(I2C_MASTER_SCL_IO);
    gpio_reset_pin(I2C_MASTER_SDA_IO);
    ESP_LOGI(TAG, "Reset magistrali I2C zakończony.");
}

static void water_sensor_init(void) {
    gpio_reset_pin(WATER_LEVEL_GPIO);
    gpio_set_direction(WATER_LEVEL_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(WATER_LEVEL_GPIO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "Zainicjalizowano czujnik wody na GPIO %d", WATER_LEVEL_GPIO);
}

static void soil_sensor_init(void) {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, SOIL_ADC_CHANNEL, &config));

    ESP_LOGI(TAG, "Zainicjalizowano czujnik gleby (ADC Oneshot) na kanale %d", SOIL_ADC_CHANNEL);
}

static esp_err_t bme280_sensor_init(void)
{
    bmp280_params_t params;
    bmp280_init_default_params(&params); 
    params.mode = BMP280_MODE_FORCED;

    esp_err_t err = bmp280_init_desc(&bme280_dev, BMP280_I2C_ADDRESS_0, I2C_MASTER_NUM, 
                                      I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Błąd inicjalizacji deskryptora BME280: %s", esp_err_to_name(err));
        return err;
    }
    return bmp280_init(&bme280_dev, &params);
}

esp_err_t sensors_init(void) {
    esp_err_t res = ESP_OK;

    // 1. Inicjalizacja GPIO i ADC
    water_sensor_init();
    soil_sensor_init();

    // Reset I2C przed sterownikiem
    i2c_bus_reset();

    // 2. I2C (i2cdev library init)
    ESP_ERROR_CHECK(i2cdev_init()); 

    // 3. VEML7700
    res = veml7700_init_desc(&veml_sensor, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    if (res == ESP_OK) {
        res = veml7700_init(&veml_sensor);
        if (res == ESP_OK) {
            s_has_veml7700 = true;
            veml7700_set_config(&veml_sensor, VEML7700_GAIN_2, VEML7700_IT_100MS, VEML7700_PERS_1);
            veml7700_set_power_saving(&veml_sensor, true, VEML7700_PSM_MODE_4);
            ESP_LOGI(TAG, "VEML7700 skonfigurowany (PSM włączone)!");
        } else {
            s_has_veml7700 = false;
             ESP_LOGE(TAG, "VEML7700 błąd init: %s", esp_err_to_name(res));
        }
    } else {
        s_has_veml7700 = false;
        ESP_LOGE(TAG, "VEML7700 błąd deskryptora: %s", esp_err_to_name(res));
    }

    // 4. BME280
    if (bme280_sensor_init() == ESP_OK) {
        s_has_bme280 = true;
        ESP_LOGI(TAG, "BME280 zainicjowany pomyślnie (Tryb FORCED)!");
    } else {
        s_has_bme280 = false;
        ESP_LOGW(TAG, "BME280 problem z inicjalizacją");
        // Nie blokujemy startu, jeśli jeden czujnik padnie
    }

    return ESP_OK;
}

telemetry_fields_mask_t sensors_get_available_fields_mask(void) {
    telemetry_fields_mask_t mask = 0;

    if (s_has_soil) {
        mask |= TELEMETRY_FIELD_SOIL;
    }

    if (s_has_bme280) {
        mask |= (TELEMETRY_FIELD_TEMP | TELEMETRY_FIELD_HUM | TELEMETRY_FIELD_PRESS);
    }

    if (s_has_veml7700) {
        mask |= TELEMETRY_FIELD_LIGHT;
    }

    if (s_has_water) {
        mask |= TELEMETRY_FIELD_WATER;
    }

    return mask;
}

void sensors_get_water_status(int *water_ok) {
    int pin_level = gpio_get_level(WATER_LEVEL_GPIO);
    // 0 = Pływak w górze (Woda jest) -> water_ok = 0
    // 1 = Pływak w dole (Brak wody) -> water_ok = 1 (Alarm)
    if (pin_level == 0) {
        *water_ok = 0; 
    } else {
        *water_ok = 1;
    }
}

void sensors_read(telemetry_data_t *data) {
    // 1. Gleba
    int raw_adc = 0;
    esp_err_t soil_err = adc_oneshot_read(adc1_handle, SOIL_ADC_CHANNEL, &raw_adc);
    if (soil_err == ESP_OK) {
        int percentage = map_val(raw_adc, SOIL_DRY_VAL, SOIL_WET_VAL, 0, 100);
        if (percentage < 0) percentage = 0;
        if (percentage > 100) percentage = 100;
        data->soil_moisture = percentage;
        ESP_LOGD(TAG, "[GLEBA] ADC: %d, Wilgotność: %d %%", raw_adc, percentage);
        s_has_soil = true;

        if (!s_prev_soil_ok) {
            if (alert_limiter_allow("sensor.soil_recovered", esp_log_timestamp(), 60 * 1000, NULL)) {
                mqtt_app_send_alert2("sensor.soil_recovered", "info", "sensor", "Soil sensor recovered");
            }
        }
        s_prev_soil_ok = true;
    } else {
        data->soil_moisture = -1;
        s_has_soil = false;
        ESP_LOGW(TAG, "[GLEBA] ADC read failed: %s", esp_err_to_name(soil_err));

        if (s_prev_soil_ok) {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("sensor.soil_read_failed", esp_log_timestamp(), 5 * 60 * 1000, &suppressed)) {
                char details[128];
                snprintf(details, sizeof(details), "{\"err\":%d,\"suppressed\":%lu}", (int)soil_err, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("sensor.soil_read_failed", "warning", "sensor", "Soil ADC read failed", details);
            }
        }
        s_prev_soil_ok = false;
    }
    
    // 2. BME280
    if (s_has_bme280) {
        bool bme_ok = false;
        esp_err_t force_err = bmp280_force_measurement(&bme280_dev);
        if (force_err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            float bme_temp = 0, bme_press = 0, bme_hum = 0;
            esp_err_t read_err = bmp280_read_float(&bme280_dev, &bme_temp, &bme_press, &bme_hum);
            if (read_err == ESP_OK) {
                data->temp = bme_temp;
                data->pressure = bme_press / 100.0f;
                data->humidity = bme_hum;
                bme_ok = true;
            } else {
                data->temp = NAN;
                data->pressure = NAN;
                data->humidity = NAN;
            }
        } else {
            data->temp = NAN;
            data->pressure = NAN;
            data->humidity = NAN;
        }

        if (bme_ok && !s_prev_bme_ok) {
            if (alert_limiter_allow("sensor.bme280_recovered", esp_log_timestamp(), 60 * 1000, NULL)) {
                mqtt_app_send_alert2("sensor.bme280_recovered", "info", "sensor", "BME280 recovered");
            }
        } else if (!bme_ok && s_prev_bme_ok) {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("sensor.bme280_read_failed", esp_log_timestamp(), 5 * 60 * 1000, &suppressed)) {
                char details[160];
                snprintf(details, sizeof(details), "{\"force_ok\":%s,\"force_err\":%d,\"suppressed\":%lu}",
                         (force_err == ESP_OK) ? "true" : "false", (int)force_err, (unsigned long)suppressed);
                mqtt_app_send_alert2_details("sensor.bme280_read_failed", "warning", "sensor", "BME280 read failed", details);
            }
        }
        s_prev_bme_ok = bme_ok;
    } else {
        data->temp = NAN;
        data->pressure = NAN;
        data->humidity = NAN;
    }

    // 3. VEML7700
    if (s_has_veml7700) {
        double lux_val = 0.0;
        bool veml_ok = false;
        veml7700_auto_adjust_gain(&veml_sensor);
        if (veml7700_read_lux(&veml_sensor, &lux_val) == ESP_OK) {
            data->light_lux = (float)lux_val;
            veml_ok = true;
        } else {
            data->light_lux = NAN;
        }

        if (veml_ok && !s_prev_veml_ok) {
            if (alert_limiter_allow("sensor.veml7700_recovered", esp_log_timestamp(), 60 * 1000, NULL)) {
                mqtt_app_send_alert2("sensor.veml7700_recovered", "info", "sensor", "VEML7700 recovered");
            }
        } else if (!veml_ok && s_prev_veml_ok) {
            uint32_t suppressed = 0;
            if (alert_limiter_allow("sensor.veml7700_read_failed", esp_log_timestamp(), 5 * 60 * 1000, &suppressed)) {
                char details[96];
                snprintf(details, sizeof(details), "{\"suppressed\":%lu}", (unsigned long)suppressed);
                mqtt_app_send_alert2_details("sensor.veml7700_read_failed", "warning", "sensor", "VEML7700 read failed", details);
            }
        }
        s_prev_veml_ok = veml_ok;
    } else {
        data->light_lux = NAN;
    }

    // 4. Woda
    int w_val = 0;
    sensors_get_water_status(&w_val);
    data->water_ok = (int16_t)w_val;
    
    // 5. Timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    data->timestamp = (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
    
    ESP_LOGI(TAG, "Odczyt: T:%.1f H:%.1f P:%.0f L:%.1f S:%d W:%d", 
             data->temp, data->humidity, data->pressure, data->light_lux, 
             data->soil_moisture, data->water_ok);
}
