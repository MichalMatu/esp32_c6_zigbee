#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_inputs.h"

typedef enum {
    GATEWAY_COMMAND_PLAN_OK = 0,
    GATEWAY_COMMAND_PLAN_UNSUPPORTED,
    GATEWAY_COMMAND_PLAN_INVALID,
} gateway_command_plan_result_t;

typedef struct {
    uint16_t cluster_id;
    gateway_input_capabilities_t capability;
    bool target_on;
} gateway_command_plan_t;

gateway_command_plan_result_t gateway_command_policy_plan(
    gateway_command_kind_t kind,
    double value,
    uint32_t transition_ms,
    gateway_command_plan_t *out);
