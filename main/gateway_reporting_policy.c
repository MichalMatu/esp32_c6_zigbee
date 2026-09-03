#include "gateway_reporting_policy.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define ZCL_CLUSTER_POWER_CONFIG 0x0001U
#define ZCL_CLUSTER_POLL_CONTROL 0x0020U
#define ZCL_CLUSTER_TEMPERATURE 0x0402U
#define ZCL_CLUSTER_REL_HUMIDITY 0x0405U

#define ZCL_ATTR_MEASURED_VALUE 0x0000U
#define ZCL_ATTR_BATTERY_PERCENT 0x0021U

#define ZCL_TYPE_UINT8 0x20U
#define ZCL_TYPE_UINT16 0x21U
#define ZCL_TYPE_INT16 0x29U
#define ZCL_MAX_REPORT_INTERVAL 0xfffeU

typedef struct {
    uint16_t cluster_id;
    gateway_measurement_kind_t kind;
    double change_scale;
    int32_t max_raw_change;
    gateway_reporting_spec_t spec;
} reporting_policy_entry_t;

static const reporting_policy_entry_t s_reporting_policy[] = {
    {
        .cluster_id = ZCL_CLUSTER_TEMPERATURE,
        .kind = GATEWAY_MEAS_TEMPERATURE,
        .change_scale = 100.0,
        .max_raw_change = INT16_MAX,
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
        .kind = GATEWAY_MEAS_HUMIDITY,
        .change_scale = 100.0,
        .max_raw_change = UINT16_MAX,
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
        .kind = GATEWAY_MEAS_BATTERY_PERCENT,
        .change_scale = 2.0,
        .max_raw_change = UINT8_MAX,
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

static const reporting_policy_entry_t *find_policy_for_kind(
    gateway_measurement_kind_t kind)
{
    for (size_t i = 0; i < sizeof(s_reporting_policy) / sizeof(s_reporting_policy[0]); ++i) {
        if (s_reporting_policy[i].kind == kind) {
            return &s_reporting_policy[i];
        }
    }
    return NULL;
}

bool gateway_reporting_policy_requires_binding(uint16_t cluster_id)
{
    return find_policy(cluster_id) != NULL || cluster_id == ZCL_CLUSTER_POLL_CONTROL;
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

gateway_reporting_plan_result_t gateway_reporting_policy_plan(
    gateway_measurement_kind_t kind,
    uint32_t min_interval_ms,
    uint32_t max_interval_ms,
    double reportable_change,
    gateway_reporting_plan_t *out)
{
    if (out == NULL) {
        return GATEWAY_REPORTING_PLAN_INVALID;
    }
    memset(out, 0, sizeof(*out));
    const reporting_policy_entry_t *entry = find_policy_for_kind(kind);
    if (entry == NULL) {
        return GATEWAY_REPORTING_PLAN_UNSUPPORTED;
    }
    if (max_interval_ms == 0U || min_interval_ms > max_interval_ms ||
        !(reportable_change >= 0.0)) {
        return GATEWAY_REPORTING_PLAN_INVALID;
    }

    bool clamped = false;
    uint64_t min_seconds = ((uint64_t)min_interval_ms + 999U) / 1000U;
    uint64_t max_seconds = (uint64_t)max_interval_ms / 1000U;
    if ((min_interval_ms % 1000U) != 0U || (max_interval_ms % 1000U) != 0U) {
        clamped = true;
    }
    if (max_seconds == 0U) {
        max_seconds = 1U;
        clamped = true;
    }
    if (min_seconds > ZCL_MAX_REPORT_INTERVAL) {
        min_seconds = ZCL_MAX_REPORT_INTERVAL;
        clamped = true;
    }
    if (max_seconds > ZCL_MAX_REPORT_INTERVAL) {
        max_seconds = ZCL_MAX_REPORT_INTERVAL;
        clamped = true;
    }
    if (max_seconds < min_seconds) {
        max_seconds = min_seconds;
        clamped = true;
    }

    const double scaled_change = reportable_change * entry->change_scale;
    int32_t raw_change;
    if (!(scaled_change >= 0.0)) {
        return GATEWAY_REPORTING_PLAN_INVALID;
    }
    if (scaled_change > (double)entry->max_raw_change) {
        raw_change = entry->max_raw_change;
        clamped = true;
    } else {
        raw_change = (int32_t)(scaled_change + 0.5);
    }
    const double effective_change = (double)raw_change / entry->change_scale;
    if (effective_change != reportable_change) {
        clamped = true;
    }

    out->cluster_id = entry->cluster_id;
    out->spec = entry->spec;
    out->spec.min_interval = (uint16_t)min_seconds;
    out->spec.max_interval = (uint16_t)max_seconds;
    out->spec.reportable_change = raw_change;
    out->effective_min_interval_ms = (uint32_t)min_seconds * 1000U;
    out->effective_max_interval_ms = (uint32_t)max_seconds * 1000U;
    out->effective_reportable_change = effective_change;
    out->clamped = clamped;
    return clamped ? GATEWAY_REPORTING_PLAN_CLAMPED : GATEWAY_REPORTING_PLAN_OK;
}
