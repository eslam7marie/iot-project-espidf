#include "sdkconfig.h"
#include "driver/gpio.h"
#include "relay.h"

#define RELAY_PIN GPIO_NUM_26

// Active HIGH: 1 means ON (our LED). Active LOW: 0 means ON (some relay modules).
#ifdef CONFIG_IOT_RELAY_ACTIVE_LOW
#define RELAY_ON_LEVEL  0
#define RELAY_OFF_LEVEL 1
#else
#define RELAY_ON_LEVEL  1
#define RELAY_OFF_LEVEL 0
#endif

// Private state: whether setup finished and what we last told the output to do.
static bool initialized = false;
static bool relay_on = false;

esp_err_t relay_init(void)
{
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Store the OFF level before enabling the output pin.
    esp_err_t result = gpio_set_level(RELAY_PIN, RELAY_OFF_LEVEL);
    if (result != ESP_OK) {
        return result;
    }

    // 2. Select GPIO26 and make it an output, with no pulls or interrupts.
    gpio_config_t settings = {0};
    // The driver uses one bit per pin. Shift a 64-bit 1 to bit 26.
    settings.pin_bit_mask = 1ULL << RELAY_PIN;
    settings.mode = GPIO_MODE_OUTPUT;
    settings.pull_up_en = GPIO_PULLUP_DISABLE;
    settings.pull_down_en = GPIO_PULLDOWN_DISABLE;
    settings.intr_type = GPIO_INTR_DISABLE;

    result = gpio_config(&settings);
    if (result != ESP_OK) {
        return result;
    }

    // Only mark initialization complete after both hardware calls succeed.
    relay_on = false;
    initialized = true;
    return ESP_OK;
}

esp_err_t relay_set(bool on)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Translate logical ON/OFF to the electrical level for this configuration.
    int level = RELAY_OFF_LEVEL;
    if (on) {
        level = RELAY_ON_LEVEL;
    }

    esp_err_t result = gpio_set_level(RELAY_PIN, level);
    if (result != ESP_OK) {
        return result;
    }

    // Remember the state only after a successful write.
    relay_on = on;
    return ESP_OK;
}

bool relay_is_on(void)
{
    return relay_on;
}
