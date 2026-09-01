#include "gateway_zigbee_input.h"

#include "gateway_zcl_value.h"

gateway_input_capabilities_t gateway_zigbee_capabilities_from_clusters(
    const uint16_t *clusters, size_t count)
{
    gateway_input_capabilities_t capabilities = 0U;
    if (clusters == NULL) {
        return capabilities;
    }
    for (size_t i = 0U; i < count; ++i) {
        capabilities |= gateway_zcl_capabilities_for_server_cluster(clusters[i]);
    }
    return capabilities;
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
