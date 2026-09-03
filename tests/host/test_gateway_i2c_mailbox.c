#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gateway_i2c_mailbox.h"
#include "gateway_link_protocol.h"

int main(void)
{
    uint8_t frame[GATEWAY_LINK_MAX_FRAME_BYTES];
    for (size_t i = 0; i < sizeof(frame); ++i) {
        frame[i] = (uint8_t)i;
    }

    uint8_t request[GATEWAY_LINK_MAX_FRAME_BYTES + GATEWAY_I2C_MAILBOX_WRITE_OVERHEAD];
    size_t request_length = 0U;
    assert(gateway_i2c_mailbox_build_write(
        frame, sizeof(frame), request, sizeof(request), &request_length));
    assert(request_length == sizeof(request));
    assert(request[0] == GATEWAY_I2C_MAILBOX_OP_WRITE_FRAME);
    assert(request[1] == 0x00U);
    assert(request[2] == 0x01U);
    assert(memcmp(&request[3], frame, sizeof(frame)) == 0);

    assert(!gateway_i2c_mailbox_build_write(
        frame, 0U, request, sizeof(request), &request_length));
    assert(!gateway_i2c_mailbox_build_write(
        frame, sizeof(frame), request, sizeof(request) - 1U, &request_length));

    size_t pending = 0U;
    const uint8_t none[2] = {0x00U, 0x00U};
    assert(gateway_i2c_mailbox_parse_pending_length(none, &pending));
    assert(pending == 0U);

    const uint8_t maximum[2] = {0x00U, 0x01U};
    assert(gateway_i2c_mailbox_parse_pending_length(maximum, &pending));
    assert(pending == GATEWAY_LINK_MAX_FRAME_BYTES);

    const uint8_t oversized[2] = {0x01U, 0x01U};
    assert(!gateway_i2c_mailbox_parse_pending_length(oversized, &pending));

    puts("gateway_i2c_mailbox host tests passed");
    return 0;
}
