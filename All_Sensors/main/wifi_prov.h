#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include <stdbool.h>

/**
 * @brief Inicjalizuje moduł Wi-Fi oraz mechanizm provisioningu (BLE).
 * 
 * Funkcja sprawdza, czy w NVS zapisane są dane logowania do Wi-Fi.
 * - Jeśli TAK: Próbuje się połączyć.
 * - Jeśli NIE: Uruchamia tryb Provisioning przez proste BLE (GATT Server).
 * 
 * Uruchamia również task monitorujący przycisk BOOT (GPIO 0).
 * - Krótkie wciśnięcie (gdy brak WiFi): otwiera okno provisioningu.
 * - Długie wciśnięcie (>3s): Czyści dane WiFi i restartuje układ.
 */
void wifi_prov_init(void);

/**
 * @brief Blokuje wykonanie do momentu nawiązania połączenia WiFi i pobrania IP.
 * 
 * @return true jeśli połączono, false w przypadku błędu/timeoutu.
 */
void wifi_prov_wait_connected(void);

#endif // WIFI_PRO_H
