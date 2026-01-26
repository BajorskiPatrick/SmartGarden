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

 // Czas integracji (Integration Time) - bity 9:6 w ALS_CONF_0 
 
typedef enum {
    VEML7700_IT_100MS = 0x00,
    VEML7700_IT_200MS = 0x01,
    VEML7700_IT_400MS = 0x02,
    VEML7700_IT_800MS = 0x03,
    VEML7700_IT_50MS  = 0x08,
    VEML7700_IT_25MS  = 0x0C
} veml7700_it_t;

  // Wzmocnienie (Gain) - bity 12:11 w ALS_CONF_0 
 
typedef enum {
    VEML7700_GAIN_1   = 0x00, // x1
    VEML7700_GAIN_2   = 0x01, // x2
    VEML7700_GAIN_1_8 = 0x02, // x1/8
    VEML7700_GAIN_1_4 = 0x03  // x1/4
} veml7700_gain_t;

   // Persistence protection (ALS_PERS) - bity 5:4 w ALS_CONF_0.
           // Liczba pomiarów poza progiem wymagana do wyzwolenia przerwania.
 
typedef enum {
    VEML7700_PERS_1 = 0x00, 
    VEML7700_PERS_2 = 0x01, 
    VEML7700_PERS_4 = 0x02, 
    VEML7700_PERS_8 = 0x03  
} veml7700_pers_t;


   // Tryby oszczędzania energii (Power Saving Mode)
 
typedef enum {
    VEML7700_PSM_MODE_1 = 0x00,
    VEML7700_PSM_MODE_2 = 0x01,
    VEML7700_PSM_MODE_3 = 0x02,
    VEML7700_PSM_MODE_4 = 0x03
} veml7700_psm_mode_t;


  // Status przerwania (odczyt z rejestru 0x06) 
 
typedef struct {
    bool was_low_threshold;  // Bit 15: Low threshold exceeded
    bool was_high_threshold; // Bit 14: High threshold exceeded
} veml7700_interrupt_status_t;


  // Główna struktura uchwytu (Handle).
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
 * @brief Inicjalizuje sterownik VEML7700.
 *
 * Ustawia domyślną konfigurację czujnika oraz weryfikuje identyfikator układu.
 *
 * @param handle Wskaźnik na uchwyt czujnika (musi wskazywać na poprawną pamięć).
 * @param i2c_port Port I2C skonfigurowany w aplikacji (np. I2C_NUM_0).
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu ESP-IDF.
 */
esp_err_t veml7700_init(veml7700_handle_t *handle, i2c_port_t i2c_port);

/**
 * @brief Odczytuje i weryfikuje ID urządzenia (Rejestr 0x07).
 *
 * @param handle Uchwyt czujnika.
 *
 * @return ESP_OK jeśli odczyt i weryfikacja ID zakończyły się sukcesem;
 *         w przeciwnym razie kod błędu (np. ESP_ERR_INVALID_RESPONSE).
 */
esp_err_t veml7700_read_id(veml7700_handle_t *handle);

/**
 * @brief Ustawia główne parametry pomiaru (Gain, Integration Time, Persistence).
 *
 * @param handle Uchwyt czujnika.
 * @param gain Wzmocnienie wejściowe (czułość).
 * @param it Czas integracji.
 * @param pers Persistence dla przerwań ALS (ile kolejnych pomiarów poza progiem).
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_set_config(veml7700_handle_t *handle, veml7700_gain_t gain, veml7700_it_t it, veml7700_pers_t pers);

/**
 * @brief Włącza lub wyłącza tryb Shutdown (ALS_SD).
 *
 * @param handle Uchwyt czujnika.
 * @param shutdown true = wyłącza ALS (shutdown), false = włącza pomiary.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_set_shutdown(veml7700_handle_t *handle, bool shutdown);

/**
 * @brief Konfiguruje Power Saving Mode (Rejestr 0x03).
 *
 * @param handle Uchwyt czujnika.
 * @param enable true = włącza PSM, false = wyłącza PSM.
 * @param mode Tryb PSM (wpływa na przebieg pomiarów wg noty).
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_set_power_saving(veml7700_handle_t *handle, bool enable, veml7700_psm_mode_t mode);

/**
 * @brief Konfiguruje przerwania ALS (progi i włączenie).
 *
 * @param handle Uchwyt czujnika.
 * @param enable true = włącza przerwania ALS, false = wyłącza.
 * @param high_threshold Próg górny (RAW) dla przerwania.
 * @param low_threshold Próg dolny (RAW) dla przerwania.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_set_interrupts(veml7700_handle_t *handle, bool enable, uint16_t high_threshold, uint16_t low_threshold);

/**
 * @brief Odczytuje status przerwań (Rejestr 0x06).
 *
 * @param handle Uchwyt czujnika.
 * @param status Wyjście: struktura, do której zostanie wpisany status przerwań.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_get_interrupt_status(veml7700_handle_t *handle, veml7700_interrupt_status_t *status);

/**
 * @brief Odczytuje surową wartość ALS (Rejestr 0x04).
 *
 * @param handle Uchwyt czujnika.
 * @param raw_als Wyjście: odebrana surowa wartość ALS.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_read_als_raw(veml7700_handle_t *handle, uint16_t *raw_als);

/**
 * @brief Odczytuje surową wartość WHITE (Rejestr 0x05).
 *
 * @param handle Uchwyt czujnika.
 * @param raw_white Wyjście: odebrana surowa wartość WHITE.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_read_white_raw(veml7700_handle_t *handle, uint16_t *raw_white);

/**
 * @brief Konwertuje surowe ALS (RAW) na luks [lx].
 *
 * Ta funkcja nie wykonuje odczytu z I2C – tylko przelicza.
 *
 * @param raw Surowa wartość ALS (np. z veml7700_read_als_raw()).
 * @param gain Gain użyty podczas tego pomiaru.
 * @param it Integration time użyty podczas tego pomiaru.
 * @param lux Wyjście: przeliczona wartość w luksach.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu (np. ESP_ERR_INVALID_ARG).
 */
esp_err_t veml7700_convert_als_raw_to_lux(uint16_t raw, veml7700_gain_t gain, veml7700_it_t it, double *lux);

/**
 * @brief Odczytuje natężenie światła w luksach [lx].
 *
 * Wykonuje pojedynczy odczyt RAW ALS. Jeśli RAW jest poza zakresem, ustawia Gain na przyszłość
 * (loguje ostrzeżenie o niepewności), ale zawsze konwertuje aktualny RAW i zwraca wynik
 * bez ponownego odczytu.
 *
 * @param handle Uchwyt czujnika.
 * @param lux Wyjście: przeliczona wartość w luksach.
 *
 * @return ESP_OK w przypadku powodzenia; w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_read_lux(veml7700_handle_t *handle, double *lux);

/**
 * @brief Automatycznie dopasowuje Gain na podstawie aktualnego odczytu RAW ALS.
 *
 * @param handle Uchwyt czujnika.
 *
 * @return ESP_OK jeśli konfiguracja nie wymagała zmiany lub zmiana została zastosowana;
 *         w przeciwnym razie kod błędu.
 */
esp_err_t veml7700_auto_adjust_gain(veml7700_handle_t *handle);

#ifdef __cplusplus
}
#endif