#include <ctype.h>
#include <stdatomic.h>       // Safely share a connection flag between tasks.
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "mqtt_command.h"
#include "mqtt_connection.h"

static const char *TAG = "mqtt_connection";
static esp_mqtt_client_handle_t client;
static QueueHandle_t command_queue;
static atomic_bool connected = false;
static mqtt_command_buffer_t command_buffer;
static char telemetry_topic[80];
static char command_topic[80];
static char status_topic[80];

// Called only by MQTT's task. Main receives a bool rather than sharing GPIO state.
static void receive_command(esp_mqtt_event_handle_t event)
{
    if (event->data_len < 0 || event->total_data_len < 0 || event->current_data_offset < 0) {
        mqtt_command_reset(&command_buffer);
        return;
    }
    bool correct_topic = false;
    if (event->current_data_offset == 0) {
        correct_topic = event->topic != NULL &&
            event->topic_len == (int)strlen(command_topic) &&
            memcmp(event->topic, command_topic, event->topic_len) == 0;
        // Commands are non-retained: don't replay an old actuator command at boot.
        if (event->retain) {
            ESP_LOGW(TAG, "Ignored retained command; publish commands without -r");
            mqtt_command_reset(&command_buffer);
            return;
        }
    }

    mqtt_command_result_t result = mqtt_command_append(&command_buffer,
        event->data, event->data_len, event->total_data_len,
        event->current_data_offset, correct_topic);
    if (result == COMMAND_REJECTED) {
        ESP_LOGW(TAG, "Ignored command: wrong topic, oversized or invalid fragments");
        return;
    }
    if (result == COMMAND_INCOMPLETE) {
        return;  // Wait for the remaining pieces before parsing JSON.
    }

    bool on;
    if (!mqtt_command_parse(command_buffer.text, command_buffer.received, &on)) {
        ESP_LOGW(TAG, "Ignored command: expected exactly {\"relay\":\"on\"} or {\"relay\":\"off\"}");
        return;
    }
    if (xQueueSend(command_queue, &on, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Command queue full; command dropped");
        return;
    }
    ESP_LOGI(TAG, "Received relay command: %s", on ? "on" : "off");
}

static void on_mqtt_event(void *argument, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    (void)argument;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        atomic_store(&connected, true);
        mqtt_command_reset(&command_buffer);
        ESP_LOGI(TAG, "Connected to Mosquitto");
        // Repeat subscription and online status after every broker reconnect.
        if (esp_mqtt_client_subscribe(client, command_topic, 1) < 0) {
            ESP_LOGE(TAG, "Could not request command subscription");
        }
        // QoS 0 matches the original sketch. store=true queues it for the MQTT task.
        if (esp_mqtt_client_enqueue(client, status_topic,
                "{\"state\":\"online\"}", 0, 0, 1, true) < 0) {
            ESP_LOGW(TAG, "Could not queue online status");
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        if (event->data_len > 0 && event->data != NULL &&
            (unsigned char)event->data[0] >= 0x80) {
            ESP_LOGE(TAG, "Broker rejected command subscription; check broker permissions");
        } else {
            ESP_LOGI(TAG, "Subscribed to %s", command_topic);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        atomic_store(&connected, false);
        mqtt_command_reset(&command_buffer);
        ESP_LOGW(TAG, "Disconnected from broker; ESP-MQTT will retry automatically");
        break;
    case MQTT_EVENT_DATA:
        receive_command(event);
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL) {
            ESP_LOGW(TAG, "MQTT error: type=%d, socket errno=%d, broker code=%d",
                event->error_handle->error_type,
                event->error_handle->esp_transport_sock_errno,
                event->error_handle->connect_return_code);
        }
        break;
    default:
        break;
    }
}

void mqtt_connection_start(void)
{
    if (client != NULL) {
        return;
    }
    const char *uri = CONFIG_IOT_MQTT_BROKER_URI;
    const char *device_id = CONFIG_IOT_DEVICE_ID;
    if (uri[0] == '\0') {
        ESP_LOGW(TAG, "Set your PC broker URI in menuconfig > IoT platform - MQTT");
        return;
    }
    if (strncmp(uri, "mqtt://", 7) != 0 || strlen(uri) <= 7 || strchr(uri, '@') != NULL) {
        ESP_LOGE(TAG, "Use mqtt://PC_LAN_IP:1883 for this local anonymous-broker lesson");
        return;
    }
    // Keep identifiers safe for topic construction and the original database field.
    size_t length = strlen(device_id);
    if (length == 0 || length > 48) {
        ESP_LOGE(TAG, "Device ID must contain 1-48 letters, digits, hyphens or underscores");
        return;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)device_id[i];
        if (!isalnum(ch) && ch != '-' && ch != '_') {
            ESP_LOGE(TAG, "Invalid character in device ID");
            return;
        }
    }
    snprintf(telemetry_topic, sizeof(telemetry_topic), "devices/%s/telemetry", device_id);
    snprintf(command_topic, sizeof(command_topic), "devices/%s/commands", device_id);
    snprintf(status_topic, sizeof(status_topic), "devices/%s/status", device_id);

    command_queue = xQueueCreate(8, sizeof(bool));
    if (command_queue == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    esp_mqtt_client_config_t settings = {0};
    settings.broker.address.uri = uri;
    settings.credentials.client_id = device_id;
    settings.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    settings.session.keepalive = 30;
    settings.session.last_will.topic = status_topic;
    settings.session.last_will.msg = "{\"state\":\"offline\"}";
    settings.session.last_will.qos = 1;
    settings.session.last_will.retain = 1;
    settings.network.reconnect_timeout_ms = 5000;
    settings.buffer.size = 256;
    settings.outbox.limit = 4096;  // Bound queued data if a connection stalls.

    client = esp_mqtt_client_init(&settings);
    if (client == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                                  on_mqtt_event, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
    ESP_LOGI(TAG, "MQTT started; telemetry topic: %s", telemetry_topic);
}

void mqtt_publish_telemetry(const sensor_reading_t *reading, bool relay_on)
{
    if (client == NULL || !atomic_load(&connected)) {
        return;  // Keep sampling offline; don't accumulate old readings.
    }

    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        ESP_LOGW(TAG, "No memory for telemetry JSON");
        return;
    }
    const char *relay_text = "off";
    if (relay_on) {
        relay_text = "on";
    }
    // ts is uptime in seconds, just like Arduino millis()/1000, not calendar time.
    bool complete = cJSON_AddStringToObject(message, "device_id", CONFIG_IOT_DEVICE_ID) &&
        cJSON_AddNumberToObject(message, "temp_c", reading->temp_c) &&
        cJSON_AddNumberToObject(message, "humidity", reading->humidity) &&
        cJSON_AddStringToObject(message, "relay", relay_text) &&
        cJSON_AddNumberToObject(message, "ts", esp_timer_get_time() / 1000000);
    char *payload = NULL;
    if (complete) {
        payload = cJSON_PrintUnformatted(message);
    }
    if (payload == NULL) {
        ESP_LOGW(TAG, "Could not create telemetry JSON");
    } else {
        // Non-retained QoS 0 preserves the original firmware's delivery behavior.
        int result = esp_mqtt_client_enqueue(client, telemetry_topic, payload,
                                             0, 0, 0, true);
        if (result >= 0) {
            // Queued is not an acknowledgment from the broker (QoS 0 has no ACK).
            ESP_LOGI(TAG, "Queued telemetry: %s", payload);
        } else {
            ESP_LOGW(TAG, "Telemetry queue failed (%d)", result);
        }
        cJSON_free(payload);  // enqueue copied the payload, so we can free it now.
    }
    cJSON_Delete(message);
}

bool mqtt_take_relay_command(bool *on, TickType_t wait_ticks)
{
    if (command_queue == NULL) {
        vTaskDelay(wait_ticks);
        return false;
    }
    return xQueueReceive(command_queue, on, wait_ticks) == pdTRUE;
}
