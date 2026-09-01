#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_link_protocol.h"

typedef enum {
    GATEWAY_LINK_STREAM_NONE = 0,
    GATEWAY_LINK_STREAM_FRAME,
    GATEWAY_LINK_STREAM_DROPPED,
} gateway_link_stream_event_t;

typedef struct {
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t length;
    bool overflow;
} gateway_link_stream_decoder_t;

void gateway_link_stream_init(gateway_link_stream_decoder_t *decoder);

gateway_link_stream_event_t gateway_link_stream_feed(
    gateway_link_stream_decoder_t *decoder,
    uint8_t byte,
    gateway_link_frame_t *frame,
    gateway_link_result_t *decode_result);
