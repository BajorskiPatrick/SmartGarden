#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int soil_moisture;
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
