#include "gateway_i2c_mailbox.h"

#include <string.h>

#include "gateway_link_protocol.h"

bool gateway_i2c_mailbox_build_write(
    const uint8_t *frame,
    size_t frame_length,
    uint8_t *request,
    size_t request_capacity,
    size_t *request_length)
{
    if (frame == NULL || request == NULL || request_length == NULL ||
        frame_length == 0U || frame_length > GATEWAY_LINK_MAX_FRAME_BYTES ||
        request_capacity < frame_length + GATEWAY_I2C_MAILBOX_WRITE_OVERHEAD) {
        return false;
    }

    request[0] = GATEWAY_I2C_MAILBOX_OP_WRITE_FRAME;
    request[1] = (uint8_t)(frame_length & 0xffU);
    request[2] = (uint8_t)((frame_length >> 8U) & 0xffU);
    memcpy(&request[3], frame, frame_length);
    *request_length = frame_length + GATEWAY_I2C_MAILBOX_WRITE_OVERHEAD;
    return true;
}

bool gateway_i2c_mailbox_parse_pending_length(
    const uint8_t reply[2],
    size_t *frame_length)
{
    if (reply == NULL || frame_length == NULL) {
        return false;
    }

    const size_t length = (size_t)reply[0] | ((size_t)reply[1] << 8U);
    if (length > GATEWAY_LINK_MAX_FRAME_BYTES) {
        return false;
    }
    *frame_length = length;
    return true;
}
