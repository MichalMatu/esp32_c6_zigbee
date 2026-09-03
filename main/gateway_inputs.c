#include "gateway_inputs.h"

#include <stdio.h>
#include <string.h>

gateway_input_id_t gateway_input_make(
    gateway_source_t source, const char *id, uint8_t channel)
{
    gateway_input_id_t input = {
        .source = source,
        .channel = channel,
    };
    if (id != NULL) {
        strncpy(input.id, id, sizeof(input.id) - 1U);
    }
    return input;
}

gateway_input_id_t gateway_input_make_zigbee(
    const uint8_t ieee[8], bool ieee_valid, uint16_t short_addr, uint8_t endpoint)
{
    gateway_input_id_t input = {
        .source = GATEWAY_SOURCE_ZIGBEE,
        .channel = endpoint,
    };
    if (ieee_valid && ieee != NULL) {
        snprintf(
            input.id, sizeof(input.id),
            "zigbee:%02x%02x%02x%02x%02x%02x%02x%02x",
            ieee[7], ieee[6], ieee[5], ieee[4],
            ieee[3], ieee[2], ieee[1], ieee[0]);
    } else {
        (void)short_addr;
    }
    return input;
}

gateway_input_capabilities_t gateway_input_capability_for_measurement(
    gateway_measurement_kind_t kind)
{
    static const gateway_input_capabilities_t capabilities[] = {
        GATEWAY_INPUT_CAP_TEMPERATURE,
        GATEWAY_INPUT_CAP_HUMIDITY,
        GATEWAY_INPUT_CAP_ILLUMINANCE,
        GATEWAY_INPUT_CAP_OCCUPANCY,
        GATEWAY_INPUT_CAP_CO2,
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE,
        GATEWAY_INPUT_CAP_BATTERY_PERCENT,
        GATEWAY_INPUT_CAP_MAINS_VOLTAGE,
        GATEWAY_INPUT_CAP_VOLTAGE,
        GATEWAY_INPUT_CAP_CURRENT,
        GATEWAY_INPUT_CAP_POWER,
        GATEWAY_INPUT_CAP_ENERGY,
        GATEWAY_INPUT_CAP_ON_OFF,
        GATEWAY_INPUT_CAP_LEVEL,
    };
    return kind < (sizeof(capabilities) / sizeof(capabilities[0])) ?
        capabilities[kind] : 0U;
}

const char *gateway_input_source_name(gateway_source_t source)
{
    switch (source) {
    case GATEWAY_SOURCE_ZIGBEE: return "zigbee";
    case GATEWAY_SOURCE_LOCAL_I2C: return "local_i2c";
    default: return "unknown";
    }
}
