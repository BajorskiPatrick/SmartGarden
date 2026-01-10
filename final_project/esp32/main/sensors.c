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
            veml7700_set_config(&veml_sensor, VEML7700_GAIN_2, VEML7700_IT_100MS, VEML7700_PERS_1);
            veml7700_set_power_saving(&veml_sensor, true, VEML7700_PSM_MODE_4);
            ESP_LOGI(TAG, "VEML7700 skonfigurowany (PSM włączone)!");
        } else {
             ESP_LOGE(TAG, "VEML7700 błąd init: %s", esp_err_to_name(res));
        }
    } else {
        ESP_LOGE(TAG, "VEML7700 błąd deskryptora: %s", esp_err_to_name(res));
    }

    // 4. BME280
    if (bme280_sensor_init() == ESP_OK) {
        ESP_LOGI(TAG, "BME280 zainicjowany pomyślnie (Tryb FORCED)!");
    } else {
        ESP_LOGW(TAG, "BME280 problem z inicjalizacją");
        // Nie blokujemy startu, jeśli jeden czujnik padnie
    }

    return ESP_OK;
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
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SOIL_ADC_CHANNEL, &raw_adc));
    int percentage = map_val(raw_adc, SOIL_DRY_VAL, SOIL_WET_VAL, 0, 100);
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    data->soil_moisture = percentage;
    ESP_LOGD(TAG, "[GLEBA] ADC: %d, Wilgotność: %d %%", raw_adc, percentage);
    
    // 2. BME280
    esp_err_t err = bmp280_force_measurement(&bme280_dev);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        float bme_temp = 0, bme_press = 0, bme_hum = 0;
        if (bmp280_read_float(&bme280_dev, &bme_temp, &bme_press, &bme_hum) == ESP_OK) {
            data->temp = bme_temp;
            data->pressure = bme_press / 100.0f; 
            data->humidity = bme_hum;
        } else {
            data->temp = 0; data->pressure = 0; data->humidity = 0;
        }
    }

    // 3. VEML7700
    double lux_val = 0.0;
    veml7700_auto_adjust_gain(&veml_sensor);
    if (veml7700_read_lux(&veml_sensor, &lux_val) == ESP_OK) {
        data->light_lux = (float)lux_val;
    } else {
        data->light_lux = -1.0;
    }

    // 4. Woda
    sensors_get_water_status(&data->water_ok);
    
    // 5. Timestamp
    data->timestamp = esp_log_timestamp();
    
    ESP_LOGI(TAG, "Odczyt: T:%.1f H:%.1f P:%.0f L:%.1f S:%d W:%d", 
             data->temp, data->humidity, data->pressure, data->light_lux, 
             data->soil_moisture, data->water_ok);
}
