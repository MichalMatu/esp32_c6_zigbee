#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_events.h"
#include "gateway_link_protocol.h"

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t payload_length;
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
} gateway_link_message_t;

bool gateway_link_message_from_event(
    const gateway_event_t *event,
    gateway_link_message_t *message);

bool gateway_link_make_hello_message(gateway_link_message_t *message);
