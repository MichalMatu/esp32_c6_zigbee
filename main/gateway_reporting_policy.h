#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_inputs.h"

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

typedef enum {
    GATEWAY_REPORTING_PLAN_OK = 0,
    GATEWAY_REPORTING_PLAN_CLAMPED,
    GATEWAY_REPORTING_PLAN_UNSUPPORTED,
    GATEWAY_REPORTING_PLAN_INVALID,
} gateway_reporting_plan_result_t;

typedef struct {
    uint16_t cluster_id;
    gateway_reporting_spec_t spec;
    uint32_t effective_min_interval_ms;
    uint32_t effective_max_interval_ms;
    double effective_reportable_change;
    bool clamped;
} gateway_reporting_plan_t;

bool gateway_reporting_policy_requires_binding(uint16_t cluster_id);
bool gateway_reporting_policy_spec(
    uint16_t cluster_id, gateway_reporting_spec_t *out);
gateway_reporting_plan_result_t gateway_reporting_policy_plan(
    gateway_measurement_kind_t kind,
    uint32_t min_interval_ms,
    uint32_t max_interval_ms,
    double reportable_change,
    gateway_reporting_plan_t *out);
