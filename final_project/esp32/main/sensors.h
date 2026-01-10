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

// Zwraca maskę pól, które są faktycznie mierzone (czujniki dostępne)
telemetry_fields_mask_t sensors_get_available_fields_mask(void);

#endif // SENSORS_H
