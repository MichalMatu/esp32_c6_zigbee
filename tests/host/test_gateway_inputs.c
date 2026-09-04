#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_inputs.h"

static void test_zigbee_ieee_identity(void)
{
    const uint8_t ieee[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const gateway_input_id_t input = gateway_input_make_zigbee(
        ieee, true, 0x1234U, 7U);
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 7U);
    assert(strcmp(input.id, "zigbee:0807060504030201") == 0);
}

static void test_zigbee_requires_ieee(void)
{
    const gateway_input_id_t input = gateway_input_make_zigbee(
        NULL, false, 0x42abU, 1U);
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 1U);
    assert(input.id[0] == '\0');
}

static void test_local_identity_is_bounded(void)
{
    const gateway_input_id_t input = gateway_input_make(
        GATEWAY_SOURCE_LOCAL_I2C,
        "scd41:001122334455:abcdefghijklmnopqrstuvwxyz", 0U);
    assert(input.source == GATEWAY_SOURCE_LOCAL_I2C);
    assert(input.channel == 0U);
    assert(input.id[GATEWAY_INPUT_ID_MAX_BYTES - 1U] == '\0');
    assert(strlen(input.id) == GATEWAY_INPUT_ID_MAX_BYTES - 1U);
}

static void test_capability_mapping(void)
{
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_TEMPERATURE) ==
           GATEWAY_INPUT_CAP_TEMPERATURE);
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_HUMIDITY) ==
           GATEWAY_INPUT_CAP_HUMIDITY);
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_CO2) ==
           GATEWAY_INPUT_CAP_CO2);
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_CONTACT_OPEN) ==
           GATEWAY_INPUT_CAP_CONTACT_OPEN);
    assert(gateway_input_capability_for_measurement((gateway_measurement_kind_t)255) == 0U);
}

int main(void)
{
    test_zigbee_ieee_identity();
    test_zigbee_requires_ieee();
    test_local_identity_is_bounded();
    test_capability_mapping();
    assert(strcmp(gateway_input_source_name(GATEWAY_SOURCE_ZIGBEE), "zigbee") == 0);
    assert(strcmp(gateway_input_source_name(GATEWAY_SOURCE_LOCAL_I2C), "local_i2c") == 0);
    puts("gateway_inputs host tests passed");
    return 0;
}
