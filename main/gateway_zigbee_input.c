#include "gateway_zigbee_input.h"

#include "gateway_reporting_policy.h"
#include "gateway_zcl_value.h"

#define ZCL_CLUSTER_ON_OFF 0x0006U

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
    }
    return profile;
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
