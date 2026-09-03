#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_control.h"
#include "gateway_link_protocol.h"
#include "gateway_link_snapshot_cache.h"
#include "gateway_link_stream.h"

static size_t encode_frame(
    uint8_t type,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t out[GATEWAY_LINK_MAX_FRAME_BYTES])
{
    gateway_link_frame_t frame = {
        .type = type,
        .sequence = sequence,
        .payload_length = payload_length,
    };
    if (payload_length != 0U) {
        assert(payload != NULL);
        memcpy(frame.payload, payload, payload_length);
    }
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(
        &frame, out, GATEWAY_LINK_MAX_FRAME_BYTES, &encoded_length) == GATEWAY_LINK_OK);
    return encoded_length;
}

static size_t encode_message(
    const gateway_link_message_t *message,
    uint32_t sequence,
    uint8_t out[GATEWAY_LINK_MAX_FRAME_BYTES])
{
    assert(message != NULL);
    return encode_frame(
        message->type, sequence, message->payload, message->payload_length, out);
}

static gateway_link_stream_event_t feed_all(
    gateway_link_stream_decoder_t *decoder,
    const uint8_t *bytes,
    size_t length,
    gateway_link_frame_t *last_frame,
    gateway_link_result_t *last_result,
    size_t *frames,
    size_t *drops)
{
    gateway_link_stream_event_t last = GATEWAY_LINK_STREAM_NONE;
    for (size_t i = 0U; i < length; ++i) {
        gateway_link_frame_t frame = {0};
        gateway_link_result_t result = GATEWAY_LINK_OK;
        const gateway_link_stream_event_t event = gateway_link_stream_feed(
            decoder, bytes[i], &frame, &result);
        if (event == GATEWAY_LINK_STREAM_FRAME) {
            ++*frames;
            *last_frame = frame;
            *last_result = result;
            last = event;
        } else if (event == GATEWAY_LINK_STREAM_DROPPED) {
            ++*drops;
            *last_result = result;
            last = event;
        }
    }
    return last;
}

static void test_handshake_fragmentation_and_ping(void)
{
    gateway_link_hello_t s3_hello = {
        .role = GATEWAY_LINK_ROLE_S3_HOST,
        .min_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN |
            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,
    };
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t payload_length = 0U;
    assert(gateway_link_encode_hello_payload(
        &s3_hello, payload, sizeof(payload), &payload_length) == GATEWAY_LINK_OK);

    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t encoded_length = encode_frame(
        GATEWAY_LINK_MSG_HELLO, 41U, payload, (uint16_t)payload_length, encoded);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    size_t frames = 0U;
    size_t drops = 0U;

    const size_t split = encoded_length / 2U;
    assert(feed_all(&decoder, encoded, split, &frame, &result, &frames, &drops) ==
        GATEWAY_LINK_STREAM_NONE);
    assert(frames == 0U && drops == 0U);
    assert(feed_all(&decoder, encoded + split, encoded_length - split,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frames == 1U && drops == 0U && frame.sequence == 41U);

    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_HELLO);
    assert(gateway_link_control_peer_compatible(&action.peer_hello));

    gateway_link_message_t ack;
    assert(gateway_link_make_hello_ack_message(&ack));
    uint8_t ack_encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t ack_length = encode_message(&ack, 100U, ack_encoded);

    gateway_link_stream_decoder_t s3_decoder;
    gateway_link_stream_init(&s3_decoder);
    frames = drops = 0U;
    assert(feed_all(&s3_decoder, ack_encoded, ack_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    gateway_link_hello_t c6_hello = {0};
    assert(frame.type == GATEWAY_LINK_MSG_HELLO_ACK && frame.sequence == 100U);
    assert(gateway_link_decode_hello_payload(
        frame.payload, frame.payload_length, &c6_hello) == GATEWAY_LINK_OK);
    assert(c6_hello.role == GATEWAY_LINK_ROLE_C6_GATEWAY);
    assert((c6_hello.features & GATEWAY_LINK_FEATURE_SNAPSHOT) != 0U);
    assert((c6_hello.features & GATEWAY_LINK_FEATURE_PERMIT_JOIN) != 0U);
    assert((c6_hello.features & GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE) != 0U);
    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) != 0U);
    assert((c6_hello.features & GATEWAY_LINK_FEATURE_COMMANDS) != 0U);

    uint16_t token_length = 0U;
    assert(gateway_link_encode_u32_payload(
        0x1234abcdU, payload, sizeof(payload), &token_length) == GATEWAY_LINK_OK);
    uint8_t ping[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t ping_length = encode_frame(
        GATEWAY_LINK_MSG_PING, 42U, payload, (uint16_t)token_length, ping);
    uint8_t request_snapshot[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t snapshot_length = encode_frame(
        GATEWAY_LINK_MSG_SNAPSHOT_REQUEST, 43U, payload, (uint16_t)token_length,
        request_snapshot);

    uint8_t concatenated[GATEWAY_LINK_MAX_FRAME_BYTES * 2U];
    memcpy(concatenated, ping, ping_length);
    memcpy(concatenated + ping_length, request_snapshot, snapshot_length);
    gateway_link_stream_init(&decoder);
    frames = drops = 0U;
    gateway_link_control_action_t parsed[2] = {0};
    size_t parsed_count = 0U;
    for (size_t i = 0U; i < ping_length + snapshot_length; ++i) {
        gateway_link_frame_t candidate = {0};
        gateway_link_result_t candidate_result = GATEWAY_LINK_OK;
        const gateway_link_stream_event_t event = gateway_link_stream_feed(
            &decoder, concatenated[i], &candidate, &candidate_result);
        if (event == GATEWAY_LINK_STREAM_FRAME) {
            assert(parsed_count < 2U);
            parsed[parsed_count++] = gateway_link_control_parse(&candidate);
            ++frames;
        } else if (event == GATEWAY_LINK_STREAM_DROPPED) {
            ++drops;
        }
    }
    assert(frames == 2U && drops == 0U && parsed_count == 2U);
    assert(parsed[0].kind == GATEWAY_LINK_CONTROL_PING);
    assert(parsed[0].token == 0x1234abcdU);
    assert(parsed[1].kind == GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST);
    assert(parsed[1].token == 0x1234abcdU);

    gateway_link_message_t pong;
    assert(gateway_link_make_pong_message(parsed[0].token, &pong));
    assert(pong.type == GATEWAY_LINK_MSG_PONG);
    uint32_t pong_token = 0U;
    assert(gateway_link_decode_u32_payload(
        pong.payload, pong.payload_length, &pong_token) == GATEWAY_LINK_OK);
    assert(pong_token == parsed[0].token);
}

static void test_corruption_overflow_and_resync(void)
{
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t payload_length = 0U;
    assert(gateway_link_encode_u32_payload(
        0x55aa1234U, payload, sizeof(payload), &payload_length) == GATEWAY_LINK_OK);
    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = encode_frame(
        GATEWAY_LINK_MSG_PING, 77U, payload, (uint16_t)payload_length, good);

    uint8_t corrupt[GATEWAY_LINK_MAX_FRAME_BYTES];
    memcpy(corrupt, good, good_length);
    assert(good_length >= 3U && corrupt[good_length - 1U] == 0U);
    const size_t mutate = good_length - 2U;
    corrupt[mutate] = corrupt[mutate] == 0xffU ? 1U : (uint8_t)(corrupt[mutate] + 1U);
    assert(corrupt[mutate] != 0U);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    size_t frames = 0U;
    size_t drops = 0U;
    (void)feed_all(&decoder, corrupt, good_length,
                   &frame, &result, &frames, &drops);
    assert(frames == 0U && drops == 1U);
    assert(feed_all(&decoder, good, good_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frames == 1U && drops == 1U && frame.sequence == 77U);

    gateway_link_stream_init(&decoder);
    frames = drops = 0U;
    for (size_t i = 0U; i < GATEWAY_LINK_MAX_FRAME_BYTES + 8U; ++i) {
        gateway_link_frame_t ignored = {0};
        gateway_link_result_t ignored_result = GATEWAY_LINK_OK;
        assert(gateway_link_stream_feed(
            &decoder, 0x7eU, &ignored, &ignored_result) == GATEWAY_LINK_STREAM_NONE);
    }
    gateway_link_frame_t ignored = {0};
    gateway_link_result_t ignored_result = GATEWAY_LINK_OK;
    assert(gateway_link_stream_feed(
        &decoder, 0U, &ignored, &ignored_result) == GATEWAY_LINK_STREAM_DROPPED);
    ++drops;
    assert(feed_all(&decoder, good, good_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frames == 1U && drops == 1U && frame.sequence == 77U);
}

static bool sequence_is_next(uint32_t previous, uint32_t current)
{
    return current == previous + 1U;
}

static void test_sequence_gap_and_wrap_preserved_on_wire(void)
{
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t payload_length = 0U;
    assert(gateway_link_encode_u32_payload(
        7U, payload, sizeof(payload), &payload_length) == GATEWAY_LINK_OK);

    const uint32_t sequences[] = {UINT32_MAX - 1U, UINT32_MAX, 0U, 2U};
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    size_t frames = 0U;
    size_t drops = 0U;
    uint32_t decoded[4] = {0};

    for (size_t n = 0U; n < 4U; ++n) {
        uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
        const size_t length = encode_frame(
            GATEWAY_LINK_MSG_PING, sequences[n], payload, (uint16_t)payload_length,
            encoded);
        const size_t before = frames;
        assert(feed_all(&decoder, encoded, length,
                        &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
        assert(frames == before + 1U && drops == 0U);
        decoded[n] = frame.sequence;
    }
    assert(memcmp(decoded, sequences, sizeof(decoded)) == 0);
    assert(sequence_is_next(decoded[0], decoded[1]));
    assert(sequence_is_next(decoded[1], decoded[2]));
    assert(!sequence_is_next(decoded[2], decoded[3]));
}

static gateway_link_input_descriptor_t descriptor(
    gateway_source_t source,
    const char *id,
    uint8_t channel,
    gateway_input_capabilities_t capabilities,
    const char *model)
{
    gateway_link_input_descriptor_t value = {
        .input = gateway_input_make(source, id, channel),
        .available = true,
        .profile = {.readable = capabilities},
    };
    if (model != NULL) {
        strncpy(value.model, model, sizeof(value.model) - 1U);
    }
    return value;
}

static void test_snapshot_replay_and_policy_control(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);
    const gateway_link_input_descriptor_t local = descriptor(
        GATEWAY_SOURCE_LOCAL_I2C, "scd4x:a12bef073b43", 0U,
        GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_HUMIDITY |
            GATEWAY_INPUT_CAP_CO2,
        "SCD41");
    const gateway_link_input_descriptor_t zigbee = descriptor(
        GATEWAY_SOURCE_ZIGBEE, "zigbee:00124b00aabbccdd", 1U,
        GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_BATTERY_PERCENT,
        "");
    assert(gateway_link_snapshot_cache_update(&cache, &local));
    assert(gateway_link_snapshot_cache_update(&cache, &zigbee));
    assert(gateway_link_snapshot_cache_count(&cache) == 2U);

    const uint32_t token = 0x98765432U;
    gateway_link_message_t begin;
    gateway_link_message_t end;
    assert(gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_SNAPSHOT_BEGIN, token, &begin));
    assert(gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_SNAPSHOT_END, token, &end));

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    uint32_t sequence = 500U;
    size_t begin_seen = 0U;
    size_t end_seen = 0U;
    size_t descriptors_seen = 0U;
    uint32_t previous_sequence = 0U;
    bool have_previous = false;

    for (size_t stage = 0U; stage < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY + 2U; ++stage) {
        gateway_link_message_t message = {0};
        bool have_message = false;
        if (stage == 0U) {
            message = begin;
            have_message = true;
        } else if (stage == GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY + 1U) {
            message = end;
            have_message = true;
        } else {
            gateway_link_input_descriptor_t cached;
            if (gateway_link_snapshot_cache_copy_slot(&cache, stage - 1U, &cached)) {
                message.type = GATEWAY_LINK_MSG_INPUT_DESCRIPTOR;
                assert(gateway_link_encode_input_descriptor_payload(
                    &cached, message.payload, sizeof(message.payload),
                    &message.payload_length) == GATEWAY_LINK_OK);
                have_message = true;
            }
        }
        if (!have_message) {
            continue;
        }

        uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
        const size_t length = encode_message(&message, sequence++, encoded);
        gateway_link_frame_t frame = {0};
        gateway_link_result_t result = GATEWAY_LINK_OK;
        size_t frames = 0U;
        size_t drops = 0U;
        assert(feed_all(&decoder, encoded, length,
                        &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
        assert(frames == 1U && drops == 0U);
        if (have_previous) {
            assert(sequence_is_next(previous_sequence, frame.sequence));
        }
        previous_sequence = frame.sequence;
        have_previous = true;

        if (frame.type == GATEWAY_LINK_MSG_SNAPSHOT_BEGIN ||
            frame.type == GATEWAY_LINK_MSG_SNAPSHOT_END) {
            uint32_t decoded_token = 0U;
            assert(gateway_link_decode_u32_payload(
                frame.payload, frame.payload_length, &decoded_token) == GATEWAY_LINK_OK);
            assert(decoded_token == token);
            if (frame.type == GATEWAY_LINK_MSG_SNAPSHOT_BEGIN) {
                ++begin_seen;
            } else {
                ++end_seen;
            }
        } else {
            gateway_link_input_descriptor_t decoded = {0};
            assert(frame.type == GATEWAY_LINK_MSG_INPUT_DESCRIPTOR);
            assert(gateway_link_decode_input_descriptor_payload(
                frame.payload, frame.payload_length, &decoded) == GATEWAY_LINK_OK);
            assert(decoded.available);
            assert(decoded.profile.readable != 0U);
            ++descriptors_seen;
        }
    }
    assert(begin_seen == 1U && end_seen == 1U && descriptors_seen == 2U);

    gateway_link_frame_t unsupported = {
        .type = GATEWAY_LINK_MSG_SET_MEASUREMENT_POLICY,
    };
    gateway_link_measurement_policy_t policy = {
        .request_id = 123U,
        .input = local.input,
        .kind = GATEWAY_MEAS_TEMPERATURE,
        .min_interval_ms = 1000U,
        .max_interval_ms = 5000U,
        .reportable_change = 0.5,
    };
    uint16_t policy_length = 0U;
    assert(gateway_link_encode_measurement_policy_payload(
        &policy, unsupported.payload, sizeof(unsupported.payload),
        &policy_length) == GATEWAY_LINK_OK);
    unsupported.payload_length = (uint16_t)policy_length;
    const gateway_link_control_action_t action = gateway_link_control_parse(&unsupported);
    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY);
    assert(action.request_id == 123U);
    assert(action.measurement_policy.kind == GATEWAY_MEAS_TEMPERATURE);
    assert(action.measurement_policy.min_interval_ms == 1000U);
    assert(action.measurement_policy.max_interval_ms == 5000U);

    gateway_link_frame_t unknown = {.type = 0x7fU};
    assert(gateway_link_control_parse(&unknown).kind == GATEWAY_LINK_CONTROL_IGNORE);
}

static void test_command_request_result_round_trip(void)
{
    gateway_link_command_request_t request = {0};
    request.request_id = 7001U;
    request.input = gateway_input_make(
        GATEWAY_SOURCE_ZIGBEE, "zigbee:00124b00aabbccdd", 1U);
    request.kind = GATEWAY_COMMAND_SET_ON_OFF;
    request.value = 1.0;
    request.transition_ms = 0U;

    gateway_link_message_t outgoing = {.type = GATEWAY_LINK_MSG_COMMAND_REQUEST};
    assert(gateway_link_encode_command_request_payload(
        &request, outgoing.payload, sizeof(outgoing.payload),
        &outgoing.payload_length) == GATEWAY_LINK_OK);
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t encoded_length = encode_message(&outgoing, 900U, encoded);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    size_t frames = 0U;
    size_t drops = 0U;
    assert(feed_all(&decoder, encoded, encoded_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frames == 1U && drops == 0U);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_COMMAND);
    assert(action.command.request_id == 7001U);
    assert(action.command.value == 1.0);

    gateway_link_message_t response;
    assert(gateway_link_make_command_result_message(
        action.request_id, GATEWAY_LINK_COMMAND_TRANSMITTED, &response));
    const size_t response_length = encode_message(&response, 901U, encoded);
    gateway_link_stream_init(&decoder);
    frames = drops = 0U;
    assert(feed_all(&decoder, encoded, response_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.type == GATEWAY_LINK_MSG_COMMAND_RESULT);
    gateway_link_command_result_t decoded = {0};
    assert(gateway_link_decode_command_result_payload(
        frame.payload, frame.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 7001U);
    assert(decoded.status == GATEWAY_LINK_COMMAND_TRANSMITTED);
}

int main(void)
{
    test_handshake_fragmentation_and_ping();
    test_corruption_overflow_and_resync();
    test_sequence_gap_and_wrap_preserved_on_wire();
    test_snapshot_replay_and_policy_control();
    test_command_request_result_round_trip();
    puts("gateway_link virtual S3 E2E host tests passed");
    return 0;
}
