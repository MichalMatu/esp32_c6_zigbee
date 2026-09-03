#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_zigbee_input.h"

static void test_capability_profile(void)
{
    const uint16_t clusters[] = {
        0x0000U, 0x0001U, 0x0006U, 0x0008U, 0x0402U, 0x0405U, 0x0b04U,
    };
    const gateway_input_capability_profile_t profile =
        gateway_zigbee_capability_profile_from_clusters(
            clusters, sizeof(clusters) / sizeof(clusters[0]));
    const gateway_input_capabilities_t expected_readable =
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE |
        GATEWAY_INPUT_CAP_BATTERY_PERCENT |
        GATEWAY_INPUT_CAP_ON_OFF |
        GATEWAY_INPUT_CAP_LEVEL |
        GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY;
    const gateway_input_capabilities_t expected_reporting =
        GATEWAY_INPUT_CAP_BATTERY_PERCENT |
        GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY;
    assert(profile.readable == expected_readable);
    assert(profile.reportable == expected_reporting);
    assert(profile.configurable == expected_reporting);
    assert(profile.commandable == (GATEWAY_INPUT_CAP_ON_OFF |
        GATEWAY_INPUT_CAP_LEVEL));

    const gateway_input_capability_profile_t empty =
        gateway_zigbee_capability_profile_from_clusters(NULL, 3U);
    assert(empty.readable == 0U);
    assert(empty.reportable == 0U);
    assert(empty.configurable == 0U);
    assert(empty.commandable == 0U);
}

static void test_stable_ieee_identity(void)
{
    const uint8_t ieee[8] = {0xdd, 0xcc, 0xbb, 0xaa, 0x00, 0x4b, 0x12, 0x00};
    gateway_input_id_t input = {0};
    assert(!gateway_zigbee_stable_input_id(ieee, false, 1U, &input));
    assert(gateway_zigbee_stable_input_id(ieee, true, 7U, &input));
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 7U);
    assert(strcmp(input.id, "zigbee:00124b00aabbccdd") == 0);

    uint8_t parsed_ieee[8] = {0};
    uint8_t endpoint = 0U;
    assert(gateway_zigbee_parse_input_identity(&input, parsed_ieee, &endpoint));
    assert(memcmp(parsed_ieee, ieee, sizeof(ieee)) == 0);
    assert(endpoint == 7U);

    input.id[8] = 'x';
    assert(!gateway_zigbee_parse_input_identity(&input, parsed_ieee, &endpoint));
    input = gateway_input_make(GATEWAY_SOURCE_LOCAL_I2C, "zigbee:00124b00aabbccdd", 7U);
    assert(!gateway_zigbee_parse_input_identity(&input, parsed_ieee, &endpoint));
}

int main(void)
{
    test_capability_profile();
    test_stable_ieee_identity();
    puts("gateway_zigbee_input host tests passed");
    return 0;
}
