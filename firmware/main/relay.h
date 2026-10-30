#pragma once
#include <stdbool.h>  // bool, true and false.
#include "esp_err.h"

// Prepare GPIO26 once and start with the relay OFF.
esp_err_t relay_init(void);

// true = ON, false = OFF. Returns ESP_OK if the GPIO write succeeds.
esp_err_t relay_set(bool on);

// Last commanded state, not physical feedback from the LED/relay.
bool relay_is_on(void);

// For now, app_main is the only caller. Add synchronization before sharing
// this state between multiple tasks or network callbacks in later milestones.
