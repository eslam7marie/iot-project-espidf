#pragma once
#include <stdbool.h>

// Call once from app_main. Starts Wi-Fi in the background and returns.
// An empty SSID leaves Wi-Fi disabled so the hardware demo can still run.
void wifi_connection_start(void);

// True after DHCP assigns an IP; false again after disconnection or IP loss.
bool wifi_connection_has_ip(void);
