#include "gateway_zigbee_input.h"

#include <string.h>

#include "gateway_reporting_policy.h"
#include "gateway_zcl_value.h"

#define ZCL_CLUSTER_ON_OFF 0x0006U
#define ZCL_CLUSTER_LEVEL_CONTROL 0x0008U

gateway_input_capability_profile_t gateway_zigbee_capability_profile_from_clusters(
    const uint16_t *clusters, size_t count)
{
    gateway_input_capability_profile_t profile = {0};
    if (clusters == NULL) {
        return profile;
    }
    for (size_t i = 0U; i < count; ++i) {
        const uint16_t cluster = clusters[i];
        profile.readable |= gateway_zcl_capabilities_for_server_cluster(cluster);

        gateway_reporting_spec_t spec;
        if (gateway_reporting_policy_spec(cluster, &spec)) {
            const gateway_input_capabilities_t capability =
                gateway_zcl_capability_for_attribute(cluster, spec.attribute_id);
            profile.reportable |= capability;
            profile.configurable |= capability;
        }
        if (cluster == ZCL_CLUSTER_ON_OFF) {
            profile.commandable |= GATEWAY_INPUT_CAP_ON_OFF;
        }
        if (cluster == ZCL_CLUSTER_LEVEL_CONTROL) {
            profile.commandable |= GATEWAY_INPUT_CAP_LEVEL;
        }
    }
    return profile;
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool gateway_zigbee_parse_input_identity(
    const gateway_input_id_t *input, uint8_t ieee[8], uint8_t *endpoint)
{
    static const char prefix[] = "zigbee:";
    if (input == NULL || ieee == NULL || endpoint == NULL ||
        input->source != GATEWAY_SOURCE_ZIGBEE || input->channel == 0U ||
        strncmp(input->id, prefix, sizeof(prefix) - 1U) != 0 ||
        strlen(input->id) != (sizeof(prefix) - 1U + 16U)) {
        return false;
    }
    const char *hex = input->id + sizeof(prefix) - 1U;
    for (size_t i = 0U; i < 8U; ++i) {
        const int high = hex_nibble(hex[i * 2U]);
        const int low = hex_nibble(hex[i * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        ieee[7U - i] = (uint8_t)((high << 4) | low);
    }
    *endpoint = input->channel;
    return true;
}

bool gateway_zigbee_stable_input_id(
    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,
    gateway_input_id_t *input)
{
    if (input == NULL || ieee == NULL || !ieee_valid) {
        return false;
    }
    *input = gateway_input_make_zigbee(ieee, true, 0U, endpoint);
    return input->id[0] != '\0';
}
