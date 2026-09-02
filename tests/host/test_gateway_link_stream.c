#include <assert.h>
#include <stdio.h>

#include "gateway_link_stream.h"

static size_t make_ping(uint32_t sequence, uint32_t token, uint8_t *encoded)
{
    gateway_link_frame_t frame = {
        .type = GATEWAY_LINK_MSG_PING,
        .sequence = sequence,
    };
    assert(gateway_link_encode_u32_payload(
        token, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    size_t length = 0U;
    assert(gateway_link_encode_frame(
        &frame, encoded, GATEWAY_LINK_MAX_FRAME_BYTES, &length) == GATEWAY_LINK_OK);
    return length;
}

static gateway_link_stream_event_t feed(
    gateway_link_stream_decoder_t *decoder,
    const uint8_t *data,
    size_t length,
    gateway_link_frame_t *frame,
    gateway_link_result_t *result)
{
    gateway_link_stream_event_t event = GATEWAY_LINK_STREAM_NONE;
    for (size_t i = 0U; i < length; ++i) {
        event = gateway_link_stream_feed(decoder, data[i], frame, result);
    }
    return event;
}

static void test_partial_frame(void)
{
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t length = make_ping(3U, 0x12345678UL, encoded);
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    assert(feed(&decoder, encoded, length / 2U, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    assert(feed(&decoder, &encoded[length / 2U], length - length / 2U, &frame, &result) ==
        GATEWAY_LINK_STREAM_FRAME);
    assert(frame.type == GATEWAY_LINK_MSG_PING);
    assert(frame.sequence == 3U);
    uint32_t token = 0U;
    assert(gateway_link_decode_u32_payload(frame.payload, frame.payload_length, &token) == GATEWAY_LINK_OK);
    assert(token == 0x12345678UL);
}

static void test_corrupt_then_resync(void)
{
    uint8_t bad[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t bad_length = make_ping(1U, 1U, bad);
    size_t corrupt = bad_length / 2U;
    if (corrupt + 1U >= bad_length) {
        corrupt = 1U;
    }
    bad[corrupt] ^= 0x01U;
    if (bad[corrupt] == 0U) {
        bad[corrupt] = 0x02U;
    }

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(2U, 2U, good);
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    assert(feed(&decoder, bad, bad_length, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 2U);
}

static void test_overflow_recovers_on_delimiter(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    for (size_t i = 0U; i < GATEWAY_LINK_MAX_FRAME_BYTES + 10U; ++i) {
        assert(gateway_link_stream_feed(&decoder, 0x7fU, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    }
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(result == GATEWAY_LINK_BUFFER_TOO_SMALL);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(9U, 10U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 9U);
}


static void test_empty_delimiters_are_ignored(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_CRC_MISMATCH;

    for (size_t i = 0U; i < 8U; ++i) {
        assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
        assert(decoder.length == 0U);
        assert(!decoder.overflow);
    }

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(20U, 21U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 20U);
}

static void test_truncated_frame_then_resync(void)
{
    uint8_t truncated[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t truncated_length = make_ping(30U, 31U, truncated);
    assert(truncated_length > 3U);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;

    assert(feed(&decoder, truncated, truncated_length - 2U, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(result != GATEWAY_LINK_OK);
    assert(decoder.length == 0U);
    assert(!decoder.overflow);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(32U, 33U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 32U);
}

static void test_exact_overflow_threshold_and_resync(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;

    for (size_t i = 0U; i < GATEWAY_LINK_MAX_FRAME_BYTES; ++i) {
        assert(gateway_link_stream_feed(&decoder, 0x7fU, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    }
    assert(decoder.length == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(!decoder.overflow);

    assert(gateway_link_stream_feed(&decoder, 0x7fU, &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    assert(decoder.length == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(decoder.overflow);
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(result == GATEWAY_LINK_BUFFER_TOO_SMALL);
    assert(decoder.length == 0U);
    assert(!decoder.overflow);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(40U, 41U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 40U);
}

static void test_invalid_arguments_drop_without_state_corruption(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;

    gateway_link_stream_init(NULL);
    assert(gateway_link_stream_feed(NULL, 0x11U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(gateway_link_stream_feed(&decoder, 0x11U, NULL, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(gateway_link_stream_feed(&decoder, 0x11U, &frame, NULL) == GATEWAY_LINK_STREAM_DROPPED);
    assert(decoder.length == 0U);
    assert(!decoder.overflow);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(50U, 51U, good);
    assert(feed(&decoder, good, good_length, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 50U);
}

int main(void)
{
    test_partial_frame();
    test_corrupt_then_resync();
    test_overflow_recovers_on_delimiter();
    test_empty_delimiters_are_ignored();
    test_truncated_frame_then_resync();
    test_exact_overflow_threshold_and_resync();
    test_invalid_arguments_drop_without_state_corruption();
    puts("gateway_link_stream host tests passed");
    return 0;
}
