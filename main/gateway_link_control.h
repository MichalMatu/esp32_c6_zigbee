#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_link_event_adapter.h"
#include "gateway_link_protocol.h"

typedef enum {
    GATEWAY_LINK_CONTROL_IGNORE = 0,
    GATEWAY_LINK_CONTROL_HELLO,
    GATEWAY_LINK_CONTROL_HELLO_ACK,
    GATEWAY_LINK_CONTROL_PING,
    GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST,
    GATEWAY_LINK_CONTROL_PERMIT_JOIN,
    GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY,
    GATEWAY_LINK_CONTROL_COMMAND,
    GATEWAY_LINK_CONTROL_INVALID,
} gateway_link_control_kind_t;

typedef struct {
    gateway_link_control_kind_t kind;
    uint32_t request_id;
    uint32_t token;
    uint8_t permit_join_seconds;
    gateway_link_hello_t peer_hello;
    gateway_link_measurement_policy_t measurement_policy;
    gateway_link_command_request_t command;
} gateway_link_control_action_t;

gateway_link_control_action_t gateway_link_control_parse(
    const gateway_link_frame_t *frame);

bool gateway_link_control_peer_compatible(const gateway_link_hello_t *peer);
bool gateway_link_make_hello_message(gateway_link_message_t *message);
bool gateway_link_make_hello_ack_message(gateway_link_message_t *message);
bool gateway_link_make_pong_message(uint32_t token, gateway_link_message_t *message);
bool gateway_link_make_command_result_message(
    uint32_t request_id,
    gateway_link_command_status_t status,
    gateway_link_message_t *message);
bool gateway_link_make_snapshot_marker_message(
    uint8_t type, uint32_t token, gateway_link_message_t *message);
bool gateway_link_make_config_result_message(
    uint32_t request_id,
    gateway_link_config_status_t status,
    gateway_link_message_t *message);
