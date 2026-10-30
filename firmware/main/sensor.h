#pragma once  // Include this header only once per source file.
#include "esp_err.h"

// Keep one ADC sample and its simulated values together.
typedef struct {
    int raw;          // 12-bit ADC count: 0 to 4095.
    float temp_c;     // Simulated temperature: 15 to 35 C.
    float humidity;   // Simulated humidity: 80 down to 30 percent.
} sensor_reading_t;

// Call once before reading the sensor. Returns ESP_OK on success.
esp_err_t sensor_init(void);

// Fill the caller's reading. On error, leave it unchanged.
esp_err_t sensor_read(sensor_reading_t *reading);

// Math only: no hardware calls. Input must be a valid ADC count (0..4095).
// This can be tested separately with known inputs such as 0 and 4095.
sensor_reading_t sensor_convert_raw(int raw);
