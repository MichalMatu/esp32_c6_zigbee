#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_control.h"

static gateway_link_frame_t make_hello(uint8_t type, gateway_link_role_t role, uint8_t min_v, uint8_t max_v)
{
    gateway_link_frame_t frame = {.type = type};
    const gateway_link_hello_t hello = {
        .role = role,
        .min_version = min_v,
        .max_version = max_v,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = 0U,
    };
    assert(gateway_link_encode_hello_payload(
        &hello, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    return frame;
}

static void test_hello_compatibility(void)
{
    gateway_link_frame_t frame = make_hello(
        GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_S3_HOST, 1U, 1U);
    gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_HELLO_ACK);
    assert(gateway_link_control_peer_compatible(&action.peer_hello));

    frame = make_hello(GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_S3_HOST, 2U, 2U);
    action = gateway_link_control_parse(&frame);
    assert(!gateway_link_control_peer_compatible(&action.peer_hello));

    frame = make_hello(GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_C6_GATEWAY, 1U, 1U);
    action = gateway_link_control_parse(&frame);
    assert(!gateway_link_control_peer_compatible(&action.peer_hello));
}

static void test_hello_builders_truthfully_advertise_permit_join(void)
{
    gateway_link_message_t message;
    assert(gateway_link_make_hello_message(&message));
    assert(message.type == GATEWAY_LINK_MSG_HELLO);
    gateway_link_hello_t hello = {0};
    assert(gateway_link_decode_hello_payload(
        message.payload, message.payload_length, &hello) == GATEWAY_LINK_OK);
    assert(hello.role == GATEWAY_LINK_ROLE_C6_GATEWAY);
    assert(hello.features == (GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN));

    assert(gateway_link_make_hello_ack_message(&message));
    assert(message.type == GATEWAY_LINK_MSG_HELLO_ACK);
    assert(gateway_link_decode_hello_payload(
        message.payload, message.payload_length, &hello) == GATEWAY_LINK_OK);
    assert(hello.features == (GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN));
}

static void test_ping_and_pong(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_PING};
    assert(gateway_link_encode_u32_payload(
        0xdeadbeefUL, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_PING);
    assert(action.token == 0xdeadbeefUL);

    gateway_link_message_t pong;
    assert(gateway_link_make_pong_message(action.token, &pong));
    uint32_t token = 0U;
    assert(gateway_link_decode_u32_payload(pong.payload, pong.payload_length, &token) == GATEWAY_LINK_OK);
    assert(token == action.token);
}

static void test_permit_join_and_result(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_PERMIT_JOIN};
    const gateway_link_permit_join_t command = {
        .request_id = 42U,
        .duration_seconds = 180U,
    };
    assert(gateway_link_encode_permit_join_payload(
        &command, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_PERMIT_JOIN);
    assert(action.request_id == 42U);
    assert(action.permit_join_seconds == 180U);

    gateway_link_message_t response;
    assert(gateway_link_make_config_result_message(
        action.request_id, GATEWAY_LINK_CONFIG_APPLIED, &response));
    gateway_link_config_result_t decoded = {0};
    assert(gateway_link_decode_config_result_payload(
        response.payload, response.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 42U);
    assert(decoded.status == GATEWAY_LINK_CONFIG_APPLIED);
}

static void test_snapshot_request_and_markers(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_SNAPSHOT_REQUEST};
    assert(gateway_link_encode_u32_payload(
        0x1234abcdUL, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST);
    assert(action.token == 0x1234abcdUL);

    gateway_link_message_t message;
    assert(gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_SNAPSHOT_BEGIN, action.token, &message));
    uint32_t token = 0U;
    assert(gateway_link_decode_u32_payload(
        message.payload, message.payload_length, &token) == GATEWAY_LINK_OK);
    assert(token == action.token);
    assert(gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_SNAPSHOT_END, action.token, &message));
    assert(!gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_PING, action.token, &message));
}

static void test_measurement_policy_truthfully_unsupported(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_SET_MEASUREMENT_POLICY};
    gateway_link_measurement_policy_t policy = {0};
    policy.request_id = 77U;
    policy.input.source = GATEWAY_SOURCE_LOCAL_I2C;
    strcpy(policy.input.id, "scd4x:a12bef073b43");
    policy.kind = GATEWAY_MEAS_TEMPERATURE;
    policy.min_interval_ms = 5000U;
    policy.max_interval_ms = 60000U;
    policy.reportable_change = 0.2;
    assert(gateway_link_encode_measurement_policy_payload(
        &policy, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED);
    assert(action.request_id == 77U);
}

static void test_malformed_control_is_invalid(void)
{
    gateway_link_frame_t frame = {
        .type = GATEWAY_LINK_MSG_PERMIT_JOIN,
        .payload_length = 1U,
        .payload = {0U},
    };
    assert(gateway_link_control_parse(&frame).kind == GATEWAY_LINK_CONTROL_INVALID);
}

int main(void)
{
    test_hello_compatibility();
    test_hello_builders_truthfully_advertise_permit_join();
    test_ping_and_pong();
    test_snapshot_request_and_markers();
    test_permit_join_and_result();
    test_measurement_policy_truthfully_unsupported();
    test_malformed_control_is_invalid();
    puts("gateway_link_control host tests passed");
    return 0;
}
