#include <string.h>
#include "cJSON.h"
#include "mqtt_command.h"

void mqtt_command_reset(mqtt_command_buffer_t *buffer)
{
    buffer->received = 0;
    buffer->expected = 0;
    buffer->active = false;
}

mqtt_command_result_t mqtt_command_append(mqtt_command_buffer_t *buffer,
    const char *chunk, size_t length, size_t total, size_t offset, bool correct_topic)
{
    // Offset zero starts a new message. Only its first chunk must contain a topic.
    if (offset == 0) {
        mqtt_command_reset(buffer);
        if (!correct_topic || total == 0 || total > MQTT_COMMAND_MAX_BYTES) {
            return COMMAND_REJECTED;
        }
        buffer->expected = total;
        buffer->active = true;
    }

    // Reject gaps, overlaps, oversized data and fragments from a discarded message.
    if (!buffer->active || chunk == NULL || length == 0 ||
        total != buffer->expected || offset != buffer->received ||
        offset > total || length > total - offset) {
        mqtt_command_reset(buffer);
        return COMMAND_REJECTED;
    }

    memcpy(buffer->text + offset, chunk, length);
    buffer->received += length;
    if (buffer->received < total) {
        return COMMAND_INCOMPLETE;
    }

    buffer->text[total] = '\0';
    buffer->active = false;
    return COMMAND_COMPLETE;
}

bool mqtt_command_parse(const char *text, size_t length, bool *on)
{
    if (text == NULL || on == NULL || length == 0 || length > MQTT_COMMAND_MAX_BYTES) {
        return false;
    }
    // Reject embedded NUL bytes, which would otherwise hide trailing input.
    if (memchr(text, '\0', length) != NULL) {
        return false;
    }
    char json_text[MQTT_COMMAND_MAX_BYTES + 1];
    memcpy(json_text, text, length);
    json_text[length] = '\0';

    // cJSON uses C strings. Reject escaped NUL as well to prevent truncated values.
    if (strstr(json_text, "\\u0000") != NULL) {
        return false;
    }

    // Require the entire input to be JSON, not a valid object followed by garbage.
    cJSON *object = cJSON_ParseWithLengthOpts(json_text, length + 1, NULL, true);
    if (object == NULL) {
        return false;
    }

    bool valid = false;
    // Exactly one key rejects duplicate relay keys and ambiguous commands.
    if (cJSON_IsObject(object) && cJSON_GetArraySize(object) == 1) {
        cJSON *relay = cJSON_GetObjectItemCaseSensitive(object, "relay");
        if (cJSON_IsString(relay)) {
            if (strcmp(relay->valuestring, "on") == 0) {
                *on = true;
                valid = true;
            } else if (strcmp(relay->valuestring, "off") == 0) {
                *on = false;
                valid = true;
            }
        }
    }
    cJSON_Delete(object);
    return valid;
}
