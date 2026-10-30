#include <stdatomic.h>  // Share IP readiness safely with app_main.
#include <string.h>              // strlen and memcpy.
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"           // Wi-Fi/IP event callbacks.
#include "esp_log.h"
#include "esp_netif.h"           // Network interface and DHCP.
#include "esp_wifi.h"
#include "nvs_flash.h"           // Flash storage used by the Wi-Fi system.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_connection.h"

static const char *TAG = "wifi_connection";
static TaskHandle_t connection_task = NULL;
static atomic_bool has_ip = false;

// This task handles connection attempts independently of the sensor/LED loop.
static void connect_task(void *argument)
{
    (void)argument;  // FreeRTOS requires this parameter; we do not use it.

    // Wait until the driver announces that station mode has started.
    // A notification is a wake-up message sent to this task by the event handler.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    while (true) {
        ESP_LOGI(TAG, "Connecting to the configured Wi-Fi network...");
        esp_err_t result = esp_wifi_connect();

        if (result == ESP_OK) {
            // The request was accepted, but connection is asynchronous.
            // Sleep until a failed attempt or a later disconnection wakes us.
            // On a healthy connection this task stays asleep indefinitely.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        } else {
            ESP_LOGW(TAG, "Connection request failed: %s", esp_err_to_name(result));
        }

        ESP_LOGI(TAG, "Retrying in %d seconds", CONFIG_IOT_WIFI_RETRY_SECONDS);
        // Only this task waits. Sampling, LED control and event handling continue.
        vTaskDelay(pdMS_TO_TICKS(CONFIG_IOT_WIFI_RETRY_SECONDS * 1000));
    }
}

// ESP-IDF calls this when something changes. Keep callbacks short; never wait here.
static void on_network_event(void *argument, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Station started (ESP32 joins your router)");
        xTaskNotifyGive(connection_task);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Joined access point; waiting for an IP address...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&has_ip, false);
        wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "Disconnected: reason=%u", (unsigned)event->reason);
        xTaskNotifyGive(connection_task);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&has_ip, true);
        ip_event_got_ip_t *event = event_data;
        // This is the ESP32's address, not the PC/broker address.
        ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        atomic_store(&has_ip, false);
        ESP_LOGW(TAG, "IP address lost; waiting for DHCP or a Wi-Fi reconnect");
    }
}

void wifi_connection_start(void)
{
    // Configuration comes from your local, Git-ignored sdkconfig file.
    // Never log the password or place it in sdkconfig.defaults.
    const char *ssid = CONFIG_IOT_WIFI_SSID;
    const char *password = CONFIG_IOT_WIFI_PASSWORD;
    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);

    if (connection_task != NULL) {
        ESP_LOGW(TAG, "Wi-Fi has already been started");
        return;
    }
    if (ssid_length == 0) {
        ESP_LOGW(TAG, "Wi-Fi disabled: set the SSID/password in menuconfig > IoT platform - Wi-Fi");
        return;
    }
    // This lesson supports personal WPA2/WPA3 networks with a passphrase.
    if (ssid_length > 32 || password_length < 8 || password_length > 63) {
        ESP_LOGE(TAG, "Use a 1-32 byte SSID and an 8-63 byte WPA2/WPA3 passphrase");
        return;
    }

    // 1. Prepare storage, the TCP/IP stack, and the default event loop.
    // Initialization failures stop the program; network outages do not.
    // Do not automatically erase NVS: it may contain other application data.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (station == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // 2. Initialize the driver, then choose station mode (join an existing AP).
    wifi_init_config_t driver_settings = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&driver_settings));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Zero-initialize unused fields, then copy the configured credentials.
    // Lengths were checked above. SSIDs may use all 32 bytes without a terminator.
    wifi_config_t network_settings = {0};
    memcpy(network_settings.sta.ssid, ssid, ssid_length);
    memcpy(network_settings.sta.password, password, password_length);
    network_settings.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    network_settings.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &network_settings));

    // 3. Register callbacks and create the worker before starting the driver,
    // so the first station-start event always has a task to wake up.
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                               on_network_event, NULL));
    BaseType_t created = xTaskCreate(connect_task, "wifi_connect", 4096,
                                     NULL, 5, &connection_task);
    if (created != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // 4. Start and return immediately. No loop here waits for the router.
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool wifi_connection_has_ip(void)
{
    return atomic_load(&has_ip);
}
