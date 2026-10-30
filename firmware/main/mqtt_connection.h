#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "sensor.h"

// Call once after Wi-Fi has an IP address. ESP-MQTT manages its own reconnects.
void mqtt_connection_start(void);

// Queue a snapshot for MQTT's task to send. Offline samples are skipped.
void mqtt_publish_telemetry(const sensor_reading_t *reading, bool relay_on);

// Wait briefly for a validated command. Only app_main actually changes the GPIO.
bool mqtt_take_relay_command(bool *on, TickType_t wait_ticks);
