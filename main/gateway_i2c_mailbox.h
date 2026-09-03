#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GATEWAY_I2C_MAILBOX_OP_WRITE_FRAME 0x01U
#define GATEWAY_I2C_MAILBOX_OP_PENDING_LENGTH 0x02U
#define GATEWAY_I2C_MAILBOX_OP_READ_FRAME 0x03U
#define GATEWAY_I2C_MAILBOX_WRITE_OVERHEAD 3U

bool gateway_i2c_mailbox_build_write(
    const uint8_t *frame,
    size_t frame_length,
    uint8_t *request,
    size_t request_capacity,
    size_t *request_length);

bool gateway_i2c_mailbox_parse_pending_length(
    const uint8_t reply[2],
    size_t *frame_length);
