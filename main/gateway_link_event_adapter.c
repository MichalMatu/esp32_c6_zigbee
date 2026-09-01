#include "gateway_link_event_adapter.h"

#include <string.h>

bool gateway_link_make_hello_message(gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_HELLO;
    const gateway_link_hello_t hello = {
        .role = GATEWAY_LINK_ROLE_C6_GATEWAY,
        .min_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = 0U,
    };
    return gateway_link_encode_hello_payload(
        &hello,
        message->payload,
        sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_message_from_event(
    const gateway_event_t *event,
    gateway_link_message_t *message)
{
    if (event == NULL || message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));

    if (event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ||
        event->kind == GATEWAY_EVENT_INPUT_UNAVAILABLE) {
        gateway_link_input_descriptor_t descriptor = {
            .input = event->input,
            .available = event->kind == GATEWAY_EVENT_INPUT_AVAILABLE,
            .capabilities = event->data.input_desc.capabilities,
        };
        strncpy(descriptor.model, event->data.input_desc.model, sizeof(descriptor.model) - 1U);
        message->type = GATEWAY_LINK_MSG_INPUT_DESCRIPTOR;
        return gateway_link_encode_input_descriptor_payload(
            &descriptor,
            message->payload,
            sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    if (event->kind == GATEWAY_EVENT_MEASUREMENT) {
        const gateway_link_measurement_t measurement = {
            .input = event->input,
            .uptime_ms = event->uptime_ms,
            .measurement = event->data.measurement,
            .quality = GATEWAY_LINK_QUALITY_VALID,
        };
        message->type = GATEWAY_LINK_MSG_MEASUREMENT;
        return gateway_link_encode_measurement_payload(
            &measurement,
            message->payload,
            sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    return false;
}
