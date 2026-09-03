#include "gateway_command_policy.h"

#include <string.h>

#define ZCL_CLUSTER_ON_OFF 0x0006U

gateway_command_plan_result_t gateway_command_policy_plan(
    gateway_command_kind_t kind,
    double value,
    uint32_t transition_ms,
    gateway_command_plan_t *out)
{
    if (out == NULL) {
        return GATEWAY_COMMAND_PLAN_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (kind != GATEWAY_COMMAND_SET_ON_OFF) {
        return GATEWAY_COMMAND_PLAN_UNSUPPORTED;
    }
    if ((value != 0.0 && value != 1.0) || transition_ms != 0U) {
        return GATEWAY_COMMAND_PLAN_INVALID;
    }
    out->cluster_id = ZCL_CLUSTER_ON_OFF;
    out->capability = GATEWAY_INPUT_CAP_ON_OFF;
    out->target_on = value == 1.0;
    return GATEWAY_COMMAND_PLAN_OK;
}
