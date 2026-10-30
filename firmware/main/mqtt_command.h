#pragma once
#include <stdbool.h>
#include <stddef.h>

#define MQTT_COMMAND_MAX_BYTES 256

// MQTT may deliver one message in several chunks. Keep them in a bounded buffer.
typedef struct {
    char text[MQTT_COMMAND_MAX_BYTES + 1];
    size_t received;
    size_t expected;
    bool active;
} mqtt_command_buffer_t;

typedef enum {
    COMMAND_INCOMPLETE,
    COMMAND_COMPLETE,
    COMMAND_REJECTED
} mqtt_command_result_t;

void mqtt_command_reset(mqtt_command_buffer_t *buffer);
mqtt_command_result_t mqtt_command_append(mqtt_command_buffer_t *buffer,
    const char *chunk, size_t length, size_t total, size_t offset, bool correct_topic);

// Pure parsing: no GPIO or networking. Invalid input leaves *on unchanged.
bool mqtt_command_parse(const char *text, size_t length, bool *on);
