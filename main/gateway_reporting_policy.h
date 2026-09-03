#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GATEWAY_REPORTING_CHANGE_S16,
    GATEWAY_REPORTING_CHANGE_U16,
    GATEWAY_REPORTING_CHANGE_U8,
} gateway_reporting_change_kind_t;

typedef struct {
    uint16_t attribute_id;
    uint8_t attribute_type;
    uint16_t min_interval;
    uint16_t max_interval;
    gateway_reporting_change_kind_t change_kind;
    int32_t reportable_change;
} gateway_reporting_spec_t;

bool gateway_reporting_policy_requires_binding(uint16_t cluster_id);
bool gateway_reporting_policy_spec(
    uint16_t cluster_id, gateway_reporting_spec_t *out);
