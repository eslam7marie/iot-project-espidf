/*
 * Relay/LED commands: copy either command into Windows CMD (not PowerShell).
 * Change the broker IP/device ID below if your local configuration changes.
 * -q 1 requests QoS 1. Do not add -r: relay commands must not be retained.
 *
 * Subscribe to this device's telemetry, commands and status (-v prints topics).
 * Keep this running in a separate CMD window; press Ctrl+C to stop.
 * "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 192.168.1.12 -p 1883 -q 1 -t "devices/esp32-livingroom-01/#" -v
 *
 * ON:
 * "C:\Program Files\mosquitto\mosquitto_pub.exe" -h 192.168.1.12 -p 1883 -q 1 -t "devices/esp32-livingroom-01/commands" -m "{\"relay\":\"on\"}"
 *
 * OFF:
 * "C:\Program Files\mosquitto\mosquitto_pub.exe" -h 192.168.1.12 -p 1883 -q 1 -t "devices/esp32-livingroom-01/commands" -m "{\"relay\":\"off\"}"
 */

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"           // Monotonic time since boot, in microseconds.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay.h"
#include "sensor.h"
#include "wifi_connection.h"
#include "mqtt_connection.h"

static const char *TAG = "iot_platform";

void app_main(void)
{
    // 1. Prepare hardware once. The relay/LED starts OFF.
    ESP_ERROR_CHECK(relay_init());
    ESP_ERROR_CHECK(sensor_init());
    ESP_LOGI(TAG, "Milestone 3: local MQTT telemetry and relay commands");
    wifi_connection_start();

    sensor_reading_t reading = {0};
    bool have_reading = false;
#ifdef CONFIG_IOT_MQTT_ENABLED
    bool mqtt_started = false;
    int64_t next_publish = esp_timer_get_time() + CONFIG_IOT_TELEMETRY_INTERVAL_MS * 1000LL;
#endif
    int64_t next_sample = 0;
#ifdef CONFIG_IOT_RELAY_DEMO
    int64_t next_toggle = esp_timer_get_time() + CONFIG_IOT_SAMPLE_INTERVAL_MS * 1000LL;
#endif

    // 2. Only this task reads the sensor or changes the relay.
    // MQTT passes commands through a queue instead of touching the GPIO itself.
    while (true) {
        int64_t now = esp_timer_get_time();
#ifdef CONFIG_IOT_MQTT_ENABLED
        if (!mqtt_started && wifi_connection_has_ip()) {
            mqtt_connection_start();
            mqtt_started = true;
        }
#endif

        if (now >= next_sample) {
            esp_err_t result = sensor_read(&reading);
            have_reading = (result == ESP_OK);
            if (have_reading) {
                const char *relay_text = "off";
                if (relay_is_on()) {
                    relay_text = "on";
                }
                ESP_LOGI(TAG, "raw=%d | simulated temp_c=%.2f humidity=%.2f | relay=%s",
                         reading.raw, reading.temp_c, reading.humidity, relay_text);
            } else {
                ESP_LOGW(TAG, "Sensor read failed: %s", esp_err_to_name(result));
            }
            next_sample = now + CONFIG_IOT_SAMPLE_INTERVAL_MS * 1000LL;
        }

#ifdef CONFIG_IOT_MQTT_ENABLED
        // Sample timing and publish timing are independent (1 s and 4 s by default).
        if (now >= next_publish) {
            if (have_reading && wifi_connection_has_ip()) {
                mqtt_publish_telemetry(&reading, relay_is_on());
            }
            next_publish = now + CONFIG_IOT_TELEMETRY_INTERVAL_MS * 1000LL;
        }
#endif

#ifdef CONFIG_IOT_RELAY_DEMO
        // Available only when MQTT is disabled, so it cannot overwrite commands.
        if (now >= next_toggle) {
            ESP_ERROR_CHECK(relay_set(!relay_is_on()));
            next_toggle = now + CONFIG_IOT_SAMPLE_INTERVAL_MS * 1000LL;
        }
#endif

        // A short queue wait yields CPU time and lets commands be applied promptly.
        bool requested_on;
        if (mqtt_take_relay_command(&requested_on, pdMS_TO_TICKS(50))) {
            ESP_ERROR_CHECK(relay_set(requested_on));
            ESP_LOGI(TAG, "Relay applied: %s", requested_on ? "on" : "off");
        }
    }
}
