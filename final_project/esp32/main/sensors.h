#ifndef SENSORS_H
#define SENSORS_H

#include "esp_err.h"
#include "common_defs.h"

// Inicjalizacja wszystkich czujników
esp_err_t sensors_init(void);

// Odczyt wszystkich danych do struktury telemetrycznej
void sensors_read(telemetry_data_t *data);

// Pomocnicza funkcja do odczytu stanu wody
void sensors_get_water_status(int *water_ok);

#endif // SENSORS_H
