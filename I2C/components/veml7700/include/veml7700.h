#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// Adres urządzenia I2C (0x10 - 7-bitowy adres slave)
#define VEML7700_I2C_ADDR 0x10

/**
 * @brief Czas integracji (Integration Time) - bity 9:6 w ALS_CONF_0 
 */
typedef enum {
    VEML7700_IT_100MS = 0x00,
    VEML7700_IT_200MS = 0x01,
    VEML7700_IT_400MS = 0x02,
    VEML7700_IT_800MS = 0x03,
    VEML7700_IT_50MS  = 0x08,
    VEML7700_IT_25MS  = 0x0C
} veml7700_it_t;

/**
 * @brief Wzmocnienie (Gain) - bity 12:11 w ALS_CONF_0 
 */
typedef enum {
    VEML7700_GAIN_1   = 0x00, // x1
    VEML7700_GAIN_2   = 0x01, // x2
    VEML7700_GAIN_1_8 = 0x02, // x1/8
    VEML7700_GAIN_1_4 = 0x03  // x1/4
} veml7700_gain_t;

/**
 * @brief Persistence protection (ALS_PERS) - bity 5:4 w ALS_CONF_0.
 * Liczba pomiarów poza progiem wymagana do wyzwolenia przerwania.
 */
typedef enum {
    VEML7700_PERS_1 = 0x00, // Trigger after 1 occurrence
    VEML7700_PERS_2 = 0x01, // Trigger after 2 occurrences
    VEML7700_PERS_4 = 0x02, // Trigger after 4 occurrences
    VEML7700_PERS_8 = 0x03  // Trigger after 8 occurrences
} veml7700_pers_t;

/**
 * @brief Tryby oszczędzania energii (Power Saving Mode)
 */
typedef enum {
    VEML7700_PSM_MODE_1 = 0x00,
    VEML7700_PSM_MODE_2 = 0x01,
    VEML7700_PSM_MODE_3 = 0x02,
    VEML7700_PSM_MODE_4 = 0x03
} veml7700_psm_mode_t;

/**
 * @brief Status przerwania (odczyt z rejestru 0x06) 
 */
typedef struct {
    bool was_low_threshold;  // Bit 15: Low threshold exceeded
    bool was_high_threshold; // Bit 14: High threshold exceeded
} veml7700_interrupt_status_t;

/**
 * @brief Główna struktura uchwytu (Handle).
 * Przechowuje stan konfiguracji, aby zapobiec nadpisywaniu bitów w rejestrze 0x00.
 */
typedef struct {
    i2c_port_t i2c_port;
    // Cache konfiguracji rejestru ALS_CONF_0
    veml7700_gain_t gain;
    veml7700_it_t integration_time;
    veml7700_pers_t persistence;
    bool interrupt_enable;
    bool shutdown;
} veml7700_handle_t;

/**
 * @brief Inicjalizacja sterownika.
 * Sprawdza ID urządzenia i ustawia domyślną bezpieczną konfigurację.
 */
esp_err_t veml7700_init(veml7700_handle_t *handle, i2c_port_t i2c_port);

/**
 * @brief Sprawdza ID urządzenia (Rejestr 0x07).
 * Powinno zwrócić ESP_OK jeśli Device ID Code == 0x81.
 */
esp_err_t veml7700_read_id(veml7700_handle_t *handle);

/**
 * @brief Ustawia główne parametry pomiaru.
 * Aktualizuje rejestr 0x00 zachowując stan pozostałych bitów.
 */
esp_err_t veml7700_set_config(veml7700_handle_t *handle, veml7700_gain_t gain, veml7700_it_t it, veml7700_pers_t pers);

/**
 * @brief Włącza lub wyłącza tryb Shutdown (ALS_SD).
 */
esp_err_t veml7700_set_shutdown(veml7700_handle_t *handle, bool shutdown);

/**
 * @brief Konfiguracja Power Saving Mode (Rejestr 0x03).
 */
esp_err_t veml7700_set_power_saving(veml7700_handle_t *handle, bool enable, veml7700_psm_mode_t mode);

/**
 * @brief Konfiguracja przerwań (Progi i Enable).
 * Włącza bit ALS_INT_EN w rejestrze 0x00 i ustawia progi w 0x01 i 0x02.
 */
esp_err_t veml7700_set_interrupts(veml7700_handle_t *handle, bool enable, uint16_t high_threshold, uint16_t low_threshold);

/**
 * @brief Odczytuje status przerwania (Rejestr 0x06).
 * Pozwala sprawdzić, który próg został przekroczony.
 */
esp_err_t veml7700_get_interrupt_status(veml7700_handle_t *handle, veml7700_interrupt_status_t *status);

/**
 * @brief Odczyt surowej wartości ALS (Rejestr 0x04).
 */
esp_err_t veml7700_read_als_raw(veml7700_handle_t *handle, uint16_t *raw_als);

/**
 * @brief Odczyt surowej wartości WHITE (Rejestr 0x05).
 */
esp_err_t veml7700_read_white_raw(veml7700_handle_t *handle, uint16_t *raw_white);

/**
 * @brief Zwraca wartość natężenia światła w luksach [lx].
 * Przelicza wartość surową uwzględniając aktualny Gain i Integration Time.
 */
esp_err_t veml7700_read_lux(veml7700_handle_t *handle, double *lux);

/**
 * @brief Rozbudowany Auto-Gain.
 * Iteracyjnie dostosowuje wzmocnienie, aby wartość surowa mieściła się w optymalnym zakresie (100 - 10000).
 */
esp_err_t veml7700_auto_adjust_gain(veml7700_handle_t *handle);

#ifdef __cplusplus
}
#endif