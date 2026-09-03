#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_inputs.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t gateway_zcl_attr_size(uint8_t type, const void *value);

gateway_input_capabilities_t gateway_zcl_capabilities_for_server_cluster(
    uint16_t cluster);

gateway_input_capabilities_t gateway_zcl_capability_for_attribute(
    uint16_t cluster, uint16_t attribute);

bool gateway_zcl_normalize(uint16_t cluster,
                           uint16_t attribute,
                           uint8_t type,
                           const void *value,
                           gateway_measurement_kind_t *kind,
                           gateway_unit_t *unit,
                           double *number);

#ifdef __cplusplus
}
#endif
