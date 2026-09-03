#include "gateway_link_control.h"

#include <string.h>

static bool make_hello(uint8_t type, gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = type;
    const gateway_link_hello_t hello = {
        .role = GATEWAY_LINK_ROLE_C6_GATEWAY,
        .min_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |
            GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE |
            GATEWAY_LINK_FEATURE_COMMANDS,
    };
    return gateway_link_encode_hello_payload(
        &hello,
        message->payload,
        sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

gateway_link_control_action_t gateway_link_control_parse(
    const gateway_link_frame_t *frame)
{
    gateway_link_control_action_t action = {0};
    if (frame == NULL) {
        action.kind = GATEWAY_LINK_CONTROL_INVALID;
        return action;
    }

    switch (frame->type) {
    case GATEWAY_LINK_MSG_HELLO:
    case GATEWAY_LINK_MSG_HELLO_ACK:
        if (gateway_link_decode_hello_payload(
                frame->payload, frame->payload_length, &action.peer_hello) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = frame->type == GATEWAY_LINK_MSG_HELLO ?
            GATEWAY_LINK_CONTROL_HELLO : GATEWAY_LINK_CONTROL_HELLO_ACK;
        return action;

    case GATEWAY_LINK_MSG_PING:
        if (gateway_link_decode_u32_payload(
                frame->payload, frame->payload_length, &action.token) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_PING;
        return action;

    case GATEWAY_LINK_MSG_SNAPSHOT_REQUEST:
        if (gateway_link_decode_u32_payload(
                frame->payload, frame->payload_length, &action.token) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST;
        return action;

    case GATEWAY_LINK_MSG_PERMIT_JOIN:
    {
        gateway_link_permit_join_t command = {0};
        if (gateway_link_decode_permit_join_payload(
                frame->payload, frame->payload_length, &command) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_PERMIT_JOIN;
        action.request_id = command.request_id;
        action.permit_join_seconds = command.duration_seconds;
        return action;
    }

    case GATEWAY_LINK_MSG_SET_MEASUREMENT_POLICY:
    {
        gateway_link_measurement_policy_t policy = {0};
        if (gateway_link_decode_measurement_policy_payload(
                frame->payload, frame->payload_length, &policy) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY;
        action.request_id = policy.request_id;
        action.measurement_policy = policy;
        return action;
    }

    case GATEWAY_LINK_MSG_COMMAND_REQUEST:
    {
        gateway_link_command_request_t command = {0};
        if (gateway_link_decode_command_request_payload(
                frame->payload, frame->payload_length, &command) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_COMMAND;
        action.request_id = command.request_id;
        action.command = command;
        return action;
    }

    default:
        action.kind = GATEWAY_LINK_CONTROL_IGNORE;
        return action;
    }
}

bool gateway_link_control_peer_compatible(const gateway_link_hello_t *peer)
{
    return peer != NULL &&
        peer->role == GATEWAY_LINK_ROLE_S3_HOST &&
        peer->min_version <= GATEWAY_LINK_PROTOCOL_VERSION &&
        peer->max_version >= GATEWAY_LINK_PROTOCOL_VERSION &&
        peer->max_frame_bytes >= GATEWAY_LINK_MAX_FRAME_BYTES;
}

bool gateway_link_make_hello_message(gateway_link_message_t *message)
{
    return make_hello(GATEWAY_LINK_MSG_HELLO, message);
}

bool gateway_link_make_hello_ack_message(gateway_link_message_t *message)
{
    return make_hello(GATEWAY_LINK_MSG_HELLO_ACK, message);
}

bool gateway_link_make_pong_message(uint32_t token, gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_PONG;
    return gateway_link_encode_u32_payload(
        token, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_make_config_result_message(
    uint32_t request_id,
    gateway_link_config_status_t status,
    gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_CONFIG_RESULT;
    const gateway_link_config_result_t result = {
        .request_id = request_id,
        .status = status,
    };
    return gateway_link_encode_config_result_payload(
        &result, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_make_command_result_message(
    uint32_t request_id,
    gateway_link_command_status_t status,
    gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_COMMAND_RESULT;
    const gateway_link_command_result_t result = {
        .request_id = request_id,
        .status = status,
    };
    return gateway_link_encode_command_result_payload(
        &result, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_make_snapshot_marker_message(
    uint8_t type, uint32_t token, gateway_link_message_t *message)
{
    if (message == NULL ||
        (type != GATEWAY_LINK_MSG_SNAPSHOT_BEGIN &&
         type != GATEWAY_LINK_MSG_SNAPSHOT_END)) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = type;
    return gateway_link_encode_u32_payload(
        token, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}
