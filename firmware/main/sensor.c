#include "sdkconfig.h"
#include "esp_adc/adc_oneshot.h"
#include "sensor.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "GPIO34 mapping below is for the classic ESP32"
#endif

#define POT_CHANNEL ADC_CHANNEL_6  // GPIO34 belongs to ADC1 channel 6.
#define ADC_MAX_COUNT 4095.0f       // Largest 12-bit value; f means float.

// A handle identifies the ADC driver instance. NULL means not initialized.
// static keeps this variable private to sensor.c.
static adc_oneshot_unit_handle_t adc_handle = NULL;

esp_err_t sensor_init(void)
{
    // Avoid creating a second ADC instance if init is called twice.
    if (adc_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Open ADC1. Zero-initialization gives unused fields their default values.
    adc_oneshot_unit_init_cfg_t adc_settings = {0};
    adc_settings.unit_id = ADC_UNIT_1;
    adc_settings.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t result = adc_oneshot_new_unit(&adc_settings, &adc_handle);
    if (result != ESP_OK) {
        return result;
    }

    // 2. Configure GPIO34 for 12-bit readings.
    // Attenuation extends the ADC input range for the potentiometer voltage.
    adc_oneshot_chan_cfg_t channel_settings = {0};
    channel_settings.bitwidth = ADC_BITWIDTH_12;
    channel_settings.atten = ADC_ATTEN_DB_12;

    result = adc_oneshot_config_channel(adc_handle, POT_CHANNEL, &channel_settings);
    if (result != ESP_OK) {
        // Release the ADC if setup failed, allowing initialization to be retried.
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return result;
    }

    return ESP_OK;
}

sensor_reading_t sensor_convert_raw(int raw)
{
    // Convert the knob position to a fraction from 0.0 to 1.0.
    float position = raw / ADC_MAX_COUNT;

    // These reproduce the Arduino project values, not real sensor measurements.
    sensor_reading_t reading;
    reading.raw = raw;
    reading.temp_c = 15.0f + position * 20.0f;
    reading.humidity = 80.0f - position * 50.0f;
    return reading;
}

esp_err_t sensor_read(sensor_reading_t *reading)
{
    // We need both a destination for the result and an initialized ADC.
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Take one sample. Never use raw if the driver reports an error.
    int raw = 0;
    esp_err_t result = adc_oneshot_read(adc_handle, POT_CHANNEL, &raw);
    if (result != ESP_OK) {
        return result;
    }

    // *reading writes the result into the caller's variable.
    *reading = sensor_convert_raw(raw);
    return ESP_OK;
}
