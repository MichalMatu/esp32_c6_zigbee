#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_inputs.h"

gateway_input_capabilities_t gateway_zigbee_capabilities_from_clusters(
    const uint16_t *clusters, size_t count);

bool gateway_zigbee_stable_input_id(
    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,
    gateway_input_id_t *input);
