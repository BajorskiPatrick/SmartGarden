#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include <stdint.h>
#include <stdbool.h>

// Bitmask pól telemetrii (do selektywnych odczytów i capabilities)
typedef uint32_t telemetry_fields_mask_t;

#define TELEMETRY_FIELD_SOIL   (1u << 0)
#define TELEMETRY_FIELD_TEMP   (1u << 1)
#define TELEMETRY_FIELD_HUM    (1u << 2)
#define TELEMETRY_FIELD_PRESS  (1u << 3)
#define TELEMETRY_FIELD_LIGHT  (1u << 4)
#define TELEMETRY_FIELD_WATER  (1u << 5)

#define TELEMETRY_FIELDS_ALL (TELEMETRY_FIELD_SOIL | TELEMETRY_FIELD_TEMP | TELEMETRY_FIELD_HUM | TELEMETRY_FIELD_PRESS | TELEMETRY_FIELD_LIGHT | TELEMETRY_FIELD_WATER)

typedef struct {
    // Dla pól opcjonalnych używamy wartości specjalnych:
    // - float: NaN oznacza "niedostępne"
    // - int: -1 oznacza "niedostępne"
    int soil_moisture; // % (0-100), -1 = niedostępne
    float temp;
    float humidity;
    float pressure;
    float light_lux;
    int water_ok; // 0 = OK, 1 = ALARM
    uint32_t timestamp;
} telemetry_data_t;

typedef struct {
    float temp_min;
    float hum_min;
    int soil_min;
    float light_min;
} sensor_thresholds_t;

#endif // COMMON_DEFS_H
