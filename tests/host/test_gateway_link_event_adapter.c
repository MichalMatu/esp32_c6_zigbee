#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_event_adapter.h"

static gateway_input_id_t scd41_input(void)
{
    gateway_input_id_t input = {0};
    input.source = GATEWAY_SOURCE_LOCAL_I2C;
    input.channel = 0U;
    strcpy(input.id, "scd4x:a12bef073b43");
    return input;
}

static void test_hello_truthfully_advertises_tx_only_stage(void)
{
    gateway_link_message_t message;
    assert(gateway_link_make_hello_message(&message));
    assert(message.type == GATEWAY_LINK_MSG_HELLO);
    gateway_link_hello_t hello = {0};
    assert(gateway_link_decode_hello_payload(
        message.payload, message.payload_length, &hello) == GATEWAY_LINK_OK);
    assert(hello.role == GATEWAY_LINK_ROLE_C6_GATEWAY);
    assert(hello.min_version == 1U && hello.max_version == 1U);
    assert(hello.max_frame_bytes == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(hello.features == 0U);
}

static void test_input_descriptor_event(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_INPUT_AVAILABLE;
    event.input = scd41_input();
    event.data.input_desc.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY | GATEWAY_INPUT_CAP_CO2;
    strcpy(event.data.input_desc.model, "SCD41");

    gateway_link_message_t message;
    assert(gateway_link_message_from_event(&event, &message));
    assert(message.type == GATEWAY_LINK_MSG_INPUT_DESCRIPTOR);
    gateway_link_input_descriptor_t decoded = {0};
    assert(gateway_link_decode_input_descriptor_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.available);
    assert(strcmp(decoded.input.id, "scd4x:a12bef073b43") == 0);
    assert(decoded.capabilities == 0x13U);
    assert(strcmp(decoded.model, "SCD41") == 0);

    event.kind = GATEWAY_EVENT_INPUT_UNAVAILABLE;
    assert(gateway_link_message_from_event(&event, &message));
    assert(gateway_link_decode_input_descriptor_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(!decoded.available);
}

static void test_measurement_event(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_MEASUREMENT;
    event.input = scd41_input();
    event.uptime_ms = 5887U;
    event.data.measurement.kind = GATEWAY_MEAS_CO2;
    event.data.measurement.unit = GATEWAY_UNIT_PPM;
    event.data.measurement.value = 1123.0;

    gateway_link_message_t message;
    assert(gateway_link_message_from_event(&event, &message));
    assert(message.type == GATEWAY_LINK_MSG_MEASUREMENT);
    gateway_link_measurement_t decoded = {0};
    assert(gateway_link_decode_measurement_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.uptime_ms == 5887U);
    assert(decoded.measurement.kind == GATEWAY_MEAS_CO2);
    assert(decoded.measurement.unit == GATEWAY_UNIT_PPM);
    assert(fabs(decoded.measurement.value - 1123.0) < 0.000001);
    assert(decoded.quality == GATEWAY_LINK_QUALITY_VALID);
}

static void test_protocol_specific_event_is_not_forwarded(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_RAW_ATTRIBUTE;
    gateway_link_message_t message;
    assert(!gateway_link_message_from_event(&event, &message));

    event.kind = GATEWAY_EVENT_DEVICE_ANNOUNCE;
    assert(!gateway_link_message_from_event(&event, &message));
}

int main(void)
{
    test_hello_truthfully_advertises_tx_only_stage();
    test_input_descriptor_event();
    test_measurement_event();
    test_protocol_specific_event_is_not_forwarded();
    puts("gateway_link_event_adapter host tests passed");
    return 0;
}
