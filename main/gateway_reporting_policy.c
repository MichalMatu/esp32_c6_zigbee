#include "gateway_reporting_policy.h"

#include <stddef.h>

#define ZCL_CLUSTER_POWER_CONFIG 0x0001U
#define ZCL_CLUSTER_POLL_CONTROL 0x0020U
#define ZCL_CLUSTER_TEMPERATURE 0x0402U
#define ZCL_CLUSTER_REL_HUMIDITY 0x0405U

#define ZCL_ATTR_MEASURED_VALUE 0x0000U
#define ZCL_ATTR_BATTERY_PERCENT 0x0021U

#define ZCL_TYPE_UINT8 0x20U
#define ZCL_TYPE_UINT16 0x21U
#define ZCL_TYPE_INT16 0x29U

#define REPORTING_TEMPERATURE 0x01U
#define REPORTING_HUMIDITY 0x02U
#define REPORTING_BATTERY_PERCENT 0x04U
#define BINDING_POLL_CONTROL 0x08U

typedef struct {
    uint16_t cluster_id;
    uint8_t state_mask;
    gateway_reporting_spec_t spec;
} reporting_policy_entry_t;

static const reporting_policy_entry_t s_reporting_policy[] = {
    {
        .cluster_id = ZCL_CLUSTER_TEMPERATURE,
        .state_mask = REPORTING_TEMPERATURE,
        .spec = {
            .attribute_id = ZCL_ATTR_MEASURED_VALUE,
            .attribute_type = ZCL_TYPE_INT16,
            .min_interval = 60U,
            .max_interval = 300U,
            .change_kind = GATEWAY_REPORTING_CHANGE_S16,
            .reportable_change = 10,
        },
    },
    {
        .cluster_id = ZCL_CLUSTER_REL_HUMIDITY,
        .state_mask = REPORTING_HUMIDITY,
        .spec = {
            .attribute_id = ZCL_ATTR_MEASURED_VALUE,
            .attribute_type = ZCL_TYPE_UINT16,
            .min_interval = 60U,
            .max_interval = 300U,
            .change_kind = GATEWAY_REPORTING_CHANGE_U16,
            .reportable_change = 100,
        },
    },
    {
        .cluster_id = ZCL_CLUSTER_POWER_CONFIG,
        .state_mask = REPORTING_BATTERY_PERCENT,
        .spec = {
            .attribute_id = ZCL_ATTR_BATTERY_PERCENT,
            .attribute_type = ZCL_TYPE_UINT8,
            .min_interval = 3600U,
            .max_interval = 21600U,
            .change_kind = GATEWAY_REPORTING_CHANGE_U8,
            .reportable_change = 2,
        },
    },
};

static const reporting_policy_entry_t *find_policy(uint16_t cluster_id)
{
    for (size_t i = 0; i < sizeof(s_reporting_policy) / sizeof(s_reporting_policy[0]); ++i) {
        if (s_reporting_policy[i].cluster_id == cluster_id) {
            return &s_reporting_policy[i];
        }
    }
    return NULL;
}

uint8_t gateway_reporting_policy_reporting_mask(uint16_t cluster_id)
{
    const reporting_policy_entry_t *entry = find_policy(cluster_id);
    return entry == NULL ? 0U : entry->state_mask;
}

uint8_t gateway_reporting_policy_binding_mask(uint16_t cluster_id)
{
    const reporting_policy_entry_t *entry = find_policy(cluster_id);
    if (entry != NULL) {
        return entry->state_mask;
    }
    return cluster_id == ZCL_CLUSTER_POLL_CONTROL ? BINDING_POLL_CONTROL : 0U;
}

bool gateway_reporting_policy_spec(
    uint16_t cluster_id, gateway_reporting_spec_t *out)
{
    if (out == NULL) {
        return false;
    }
    const reporting_policy_entry_t *entry = find_policy(cluster_id);
    if (entry == NULL) {
        return false;
    }
    *out = entry->spec;
    return true;
}
