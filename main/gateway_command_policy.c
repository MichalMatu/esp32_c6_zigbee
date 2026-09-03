#include "gateway_command_policy.h"

#include <string.h>

#define ZCL_CLUSTER_ON_OFF 0x0006U
#define ZCL_CLUSTER_LEVEL_CONTROL 0x0008U
#define ZCL_LEVEL_MAX 254U
#define ZCL_TRANSITION_MAX 0xfffeU

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
    out->kind = kind;
    if (kind == GATEWAY_COMMAND_SET_ON_OFF) {
        if ((value != 0.0 && value != 1.0) || transition_ms != 0U) {
            return GATEWAY_COMMAND_PLAN_INVALID;
        }
        out->cluster_id = ZCL_CLUSTER_ON_OFF;
        out->capability = GATEWAY_INPUT_CAP_ON_OFF;
        out->target_on = value == 1.0;
        return GATEWAY_COMMAND_PLAN_OK;
    }
    if (kind == GATEWAY_COMMAND_SET_LEVEL) {
        if (!(value >= 0.0 && value <= 100.0) ||
            transition_ms % 100U != 0U ||
            transition_ms / 100U > ZCL_TRANSITION_MAX) {
            return GATEWAY_COMMAND_PLAN_INVALID;
        }
        out->cluster_id = ZCL_CLUSTER_LEVEL_CONTROL;
        out->capability = GATEWAY_INPUT_CAP_LEVEL;
        out->level = (uint8_t)(value * (double)ZCL_LEVEL_MAX / 100.0 + 0.5);
        out->transition_time = (uint16_t)(transition_ms / 100U);
        return GATEWAY_COMMAND_PLAN_OK;
    }
    return GATEWAY_COMMAND_PLAN_UNSUPPORTED;
}
