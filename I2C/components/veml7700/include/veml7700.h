#pragma once

#include "driver/i2c.h"
#include "esp_err.h"

// Adres urządzenia I2C (0x10 zgodnie z dokumentacją) 
#define VEML7700_I2C_ADDR 0x10

// Definicje czasów integracji (Integration Time) 
typedef enum {
    VEML7700_IT_100MS = 0x00,
    VEML7700_IT_200MS = 0x01,
    VEML7700_IT_400MS = 0x02,
    VEML7700_IT_800MS = 0x03,
    VEML7700_IT_50MS  = 0x08,
    VEML7700_IT_25MS  = 0x0C
} veml7700_it_t;

// Definicje wzmocnienia (Gain)
typedef enum {
    VEML7700_GAIN_1   = 0x00, // x1
    VEML7700_GAIN_2   = 0x01, // x2
    VEML7700_GAIN_1_8 = 0x02, // x1/8
    VEML7700_GAIN_1_4 = 0x03  // x1/4
} veml7700_gain_t;

// Definicje Power Saving Mode
typedef enum {
    VEML7700_PSM_MODE_1 = 0x00,
    VEML7700_PSM_MODE_2 = 0x01,
    VEML7700_PSM_MODE_3 = 0x02,
    VEML7700_PSM_MODE_4 = 0x03
} veml7700_psm_mode_t;

// Główna struktura konfiguracyjna
typedef struct {
    i2c_port_t i2c_port;
    veml7700_gain_t gain;
    veml7700_it_t integration_time;
} veml7700_handle_t;

/**
 * @brief Inicjalizacja i konfiguracja czujnika
 */
esp_err_t veml7700_init(veml7700_handle_t *handle, i2c_port_t port);

/**
 * @brief Ustawienie parametrów pomiaru (Gain i Integration Time)
 */
esp_err_t veml7700_set_config(veml7700_handle_t *handle, veml7700_gain_t gain, veml7700_it_t it);

/**
 * @brief Konfiguracja trybu oszczędzania energii
 */
esp_err_t veml7700_set_power_saving(veml7700_handle_t *handle, bool enable, veml7700_psm_mode_t mode);

/**
 * @brief Ustawienie progów przerwań (High/Low Thresholds)
 */
esp_err_t veml7700_set_interrupts(veml7700_handle_t *handle, bool enable, uint16_t high_threshold, uint16_t low_threshold);

/**
 * @brief Odczyt surowej wartości ALS (Ambient Light Sensor)
 */
esp_err_t veml7700_read_als_raw(veml7700_handle_t *handle, uint16_t *raw_als);

/**
 * @brief Odczyt surowej wartości kanału WHITE
 */
esp_err_t veml7700_read_white_raw(veml7700_handle_t *handle, uint16_t *raw_white);

/**
 * @brief Odczyt przeliczonej wartości w luksach [lx]
 * Funkcja automatycznie przelicza surowe dane w oparciu o aktualny Gain i IT.
 */
esp_err_t veml7700_read_lux(veml7700_handle_t *handle, double *lux);

/**
 * @brief Automatyczny dobór wzmocnienia (Auto-Gain)
 * Prosta implementacja dostosowująca Gain, jeśli wynik jest zbyt niski lub nasycony.
 */
esp_err_t veml7700_auto_adjust_gain(veml7700_handle_t *handle);