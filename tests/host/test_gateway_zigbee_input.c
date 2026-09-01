#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_zigbee_input.h"

static void test_capabilities(void)
{
    const uint16_t clusters[] = {
        0x0000U, 0x0001U, 0x0402U, 0x0405U, 0x0b04U,
    };
    const gateway_input_capabilities_t expected =
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE |
        GATEWAY_INPUT_CAP_BATTERY_PERCENT |
        GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY;
    assert(gateway_zigbee_capabilities_from_clusters(
        clusters, sizeof(clusters) / sizeof(clusters[0])) == expected);
    assert(gateway_zigbee_capabilities_from_clusters(NULL, 3U) == 0U);
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
}

int main(void)
{
    test_capabilities();
    test_stable_ieee_identity();
    puts("gateway_zigbee_input host tests passed");
    return 0;
}
