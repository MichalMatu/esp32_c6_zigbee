#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_protocol.h"

static gateway_input_id_t scd41_input(void)
{
    gateway_input_id_t input = {0};
    input.source = GATEWAY_SOURCE_LOCAL_I2C;
    input.channel = 0U;
    strcpy(input.id, "scd4x:a12bef073b43");
    return input;
}

static void test_crc_known_vector(void)
{
    static const uint8_t text[] = "123456789";
    assert(gateway_link_crc32(text, 9U) == 0xcbf43926UL);
}

static void test_frame_round_trip_and_cobs_zero_safety(void)
{
    gateway_link_frame_t frame = {0};
    frame.type = GATEWAY_LINK_MSG_MEASUREMENT;
    frame.flags = 0x5aU;
    frame.sequence = 0x01020304UL;
    frame.payload_length = 7U;
    const uint8_t source[] = {0x11U, 0x00U, 0x22U, 0x00U, 0x00U, 0x33U, 0x44U};
    memcpy(frame.payload, source, sizeof(source));

    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(&frame, encoded, sizeof(encoded), &encoded_length) == GATEWAY_LINK_OK);
    assert(encoded_length <= GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(encoded[encoded_length - 1U] == 0U);
    for (size_t i = 0U; i + 1U < encoded_length; ++i) assert(encoded[i] != 0U);

    gateway_link_frame_t decoded = {0};
    assert(gateway_link_decode_frame(encoded, encoded_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.type == frame.type);
    assert(decoded.flags == frame.flags);
    assert(decoded.sequence == frame.sequence);
    assert(decoded.payload_length == frame.payload_length);
    assert(memcmp(decoded.payload, source, sizeof(source)) == 0);
}

static void test_frame_crc_rejects_corruption(void)
{
    gateway_link_frame_t frame = {0};
    frame.type = GATEWAY_LINK_MSG_PING;
    frame.sequence = 7U;
    frame.payload_length = 4U;
    frame.payload[0] = 1U;
    frame.payload[1] = 2U;
    frame.payload[2] = 3U;
    frame.payload[3] = 4U;

    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(&frame, encoded, sizeof(encoded), &encoded_length) == GATEWAY_LINK_OK);
    assert(encoded_length > 8U);
    encoded[7] ^= 0x40U;

    gateway_link_frame_t decoded = {0};
    const gateway_link_result_t result = gateway_link_decode_frame(encoded, encoded_length, &decoded);
    assert(result == GATEWAY_LINK_CRC_MISMATCH || result == GATEWAY_LINK_MALFORMED);
}

static void test_hello_round_trip(void)
{
    const gateway_link_hello_t hello = {
        .role = GATEWAY_LINK_ROLE_C6_GATEWAY,
        .min_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = GATEWAY_LINK_FEATURE_SNAPSHOT |
            GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |
            GATEWAY_LINK_FEATURE_PERMIT_JOIN |
            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,
    };
    uint8_t payload[32];
    uint16_t length = 0U;
    assert(gateway_link_encode_hello_payload(&hello, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);
    gateway_link_hello_t decoded = {0};
    assert(gateway_link_decode_hello_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.role == hello.role);
    assert(decoded.min_version == GATEWAY_LINK_PROTOCOL_VERSION &&
           decoded.max_version == GATEWAY_LINK_PROTOCOL_VERSION);
    assert(decoded.max_frame_bytes == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(decoded.features == hello.features);
}

static void test_scd41_descriptor_round_trip(void)
{
    gateway_link_input_descriptor_t descriptor = {0};
    descriptor.input = scd41_input();
    descriptor.available = true;
    descriptor.profile.readable = GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY |
        GATEWAY_INPUT_CAP_CO2;
    descriptor.profile.reportable = GATEWAY_INPUT_CAP_TEMPERATURE;
    descriptor.profile.configurable = GATEWAY_INPUT_CAP_TEMPERATURE;
    strcpy(descriptor.manufacturer, "Sensirion");
    strcpy(descriptor.model, "SCD41");

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_input_descriptor_payload(
        &descriptor, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);

    gateway_link_input_descriptor_t decoded = {0};
    assert(gateway_link_decode_input_descriptor_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.input.source == GATEWAY_SOURCE_LOCAL_I2C);
    assert(decoded.input.channel == 0U);
    assert(strcmp(decoded.input.id, "scd4x:a12bef073b43") == 0);
    assert(decoded.available);
    assert(decoded.profile.readable == 0x13U);
    assert(decoded.profile.reportable == GATEWAY_INPUT_CAP_TEMPERATURE);
    assert(decoded.profile.configurable == GATEWAY_INPUT_CAP_TEMPERATURE);
    assert(decoded.profile.commandable == 0U);
    assert(strcmp(decoded.manufacturer, "Sensirion") == 0);
    assert(strcmp(decoded.model, "SCD41") == 0);
}

static void test_scd41_measurement_round_trip(void)
{
    gateway_link_measurement_t measurement = {0};
    measurement.input = scd41_input();
    measurement.uptime_ms = 5887U;
    measurement.measurement.kind = GATEWAY_MEAS_CO2;
    measurement.measurement.unit = GATEWAY_UNIT_PPM;
    measurement.measurement.value = 1123.0;
    measurement.quality = GATEWAY_LINK_QUALITY_VALID;

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_measurement_payload(
        &measurement, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);

    gateway_link_measurement_t decoded = {0};
    assert(gateway_link_decode_measurement_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(strcmp(decoded.input.id, measurement.input.id) == 0);
    assert(decoded.uptime_ms == 5887U);
    assert(decoded.measurement.kind == GATEWAY_MEAS_CO2);
    assert(decoded.measurement.unit == GATEWAY_UNIT_PPM);
    assert(fabs(decoded.measurement.value - 1123.0) < 0.000001);
    assert(decoded.quality == GATEWAY_LINK_QUALITY_VALID);
}

static void test_measurement_policy_round_trip(void)
{
    gateway_link_measurement_policy_t policy = {0};
    policy.request_id = 0x11223344UL;
    policy.input = scd41_input();
    policy.kind = GATEWAY_MEAS_TEMPERATURE;
    policy.min_interval_ms = 5000U;
    policy.max_interval_ms = 60000U;
    policy.reportable_change = 0.2;

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_measurement_policy_payload(
        &policy, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);

    gateway_link_measurement_policy_t decoded = {0};
    assert(gateway_link_decode_measurement_policy_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == policy.request_id);
    assert(strcmp(decoded.input.id, policy.input.id) == 0);
    assert(decoded.kind == GATEWAY_MEAS_TEMPERATURE);
    assert(decoded.min_interval_ms == 5000U);
    assert(decoded.max_interval_ms == 60000U);
    assert(fabs(decoded.reportable_change - 0.2) < 0.000001);
}

static void test_small_buffer_and_unknown_source_fail(void)
{
    gateway_link_input_descriptor_t descriptor = {0};
    descriptor.input = scd41_input();
    descriptor.input.source = (gateway_source_t)99;
    strcpy(descriptor.model, "SCD41");
    uint8_t payload[8];
    uint16_t length = 0U;
    assert(gateway_link_encode_input_descriptor_payload(
        &descriptor, payload, sizeof(payload), &length) != GATEWAY_LINK_OK);

    gateway_link_frame_t frame = {0};
    uint8_t encoded[4];
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(&frame, encoded, sizeof(encoded), &encoded_length) == GATEWAY_LINK_BUFFER_TOO_SMALL);
}

int main(void)
{
    test_crc_known_vector();
    test_frame_round_trip_and_cobs_zero_safety();
    test_frame_crc_rejects_corruption();
    test_hello_round_trip();
    test_scd41_descriptor_round_trip();
    test_scd41_measurement_round_trip();
    test_measurement_policy_round_trip();
    test_small_buffer_and_unknown_source_fail();
    puts("gateway_link_protocol host tests passed");
    return 0;
}
