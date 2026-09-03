#include "gateway_link_event_adapter.h"

#include <string.h>

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
            .profile = event->data.input_desc.profile,
        };
        strncpy(
            descriptor.manufacturer, event->data.input_desc.manufacturer,
            sizeof(descriptor.manufacturer) - 1U);
        strncpy(descriptor.model, event->data.input_desc.model, sizeof(descriptor.model) - 1U);
        message->type = GATEWAY_LINK_MSG_INPUT_DESCRIPTOR;
        return gateway_link_encode_input_descriptor_payload(
            &descriptor,
            message->payload,
            sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    if (event->kind == GATEWAY_EVENT_REPORTING_CONFIG &&
        event->data.reporting.request_id != 0U &&
        event->data.reporting.result != GATEWAY_EVENT_CONFIG_NONE) {
        gateway_link_config_status_t status = GATEWAY_LINK_CONFIG_ERROR;
        switch (event->data.reporting.result) {
        case GATEWAY_EVENT_CONFIG_APPLIED:
            status = GATEWAY_LINK_CONFIG_APPLIED;
            break;
        case GATEWAY_EVENT_CONFIG_CLAMPED:
            status = GATEWAY_LINK_CONFIG_CLAMPED;
            break;
        case GATEWAY_EVENT_CONFIG_UNSUPPORTED:
            status = GATEWAY_LINK_CONFIG_UNSUPPORTED;
            break;
        case GATEWAY_EVENT_CONFIG_ERROR:
        case GATEWAY_EVENT_CONFIG_NONE:
        default:
            status = GATEWAY_LINK_CONFIG_ERROR;
            break;
        }
        message->type = GATEWAY_LINK_MSG_CONFIG_RESULT;
        const gateway_link_config_result_t result = {
            .request_id = event->data.reporting.request_id,
            .status = status,
        };
        return gateway_link_encode_config_result_payload(
            &result, message->payload, sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    if (event->kind == GATEWAY_EVENT_COMMAND_RESULT &&
        event->data.command.request_id != 0U) {
        gateway_link_command_status_t status = GATEWAY_LINK_COMMAND_ERROR;
        switch (event->data.command.result) {
        case GATEWAY_EVENT_COMMAND_TRANSMITTED:
            status = GATEWAY_LINK_COMMAND_TRANSMITTED;
            break;
        case GATEWAY_EVENT_COMMAND_UNSUPPORTED:
            status = GATEWAY_LINK_COMMAND_UNSUPPORTED;
            break;
        case GATEWAY_EVENT_COMMAND_INVALID:
            status = GATEWAY_LINK_COMMAND_INVALID;
            break;
        case GATEWAY_EVENT_COMMAND_ERROR:
        default:
            status = GATEWAY_LINK_COMMAND_ERROR;
            break;
        }
        message->type = GATEWAY_LINK_MSG_COMMAND_RESULT;
        const gateway_link_command_result_t result = {
            .request_id = event->data.command.request_id,
            .status = status,
        };
        return gateway_link_encode_command_result_payload(
            &result, message->payload, sizeof(message->payload),
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
