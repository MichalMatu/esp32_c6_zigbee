from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}: {old[:140]!r}")
    p.write_text(text.replace(old, new, count))


def replace_between(path, start_marker, end_marker, new_text):
    p = Path(path)
    text = p.read_text()
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    p.write_text(text[:start] + new_text + text[end:])


# Pure measurement-policy -> ZCL Configure Reporting translation.
Path("main/gateway_reporting_policy.h").write_text(r'''#pragma once

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
''')

Path("main/gateway_reporting_policy.c").write_text(r'''#include "gateway_reporting_policy.h"

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
''')

# Stable input identity parsing for runtime routing.
replace(
    "main/gateway_zigbee_input.h",
    "bool gateway_zigbee_stable_input_id(\n    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,\n    gateway_input_id_t *input);",
    "bool gateway_zigbee_stable_input_id(\n    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,\n    gateway_input_id_t *input);\nbool gateway_zigbee_parse_input_identity(\n    const gateway_input_id_t *input, uint8_t ieee[8], uint8_t *endpoint);",
)
replace(
    "main/gateway_zigbee_input.c",
    "#include \"gateway_reporting_policy.h\"",
    "#include <string.h>\n\n#include \"gateway_reporting_policy.h\"",
)
replace(
    "main/gateway_zigbee_input.c",
    "bool gateway_zigbee_stable_input_id(\n",
    r'''static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool gateway_zigbee_parse_input_identity(
    const gateway_input_id_t *input, uint8_t ieee[8], uint8_t *endpoint)
{
    static const char prefix[] = "zigbee:";
    if (input == NULL || ieee == NULL || endpoint == NULL ||
        input->source != GATEWAY_SOURCE_ZIGBEE || input->channel == 0U ||
        strncmp(input->id, prefix, sizeof(prefix) - 1U) != 0 ||
        strlen(input->id) != (sizeof(prefix) - 1U + 16U)) {
        return false;
    }
    const char *hex = input->id + sizeof(prefix) - 1U;
    for (size_t i = 0U; i < 8U; ++i) {
        const int high = hex_nibble(hex[i * 2U]);
        const int low = hex_nibble(hex[i * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        ieee[7U - i] = (uint8_t)((high << 4) | low);
    }
    *endpoint = input->channel;
    return true;
}

bool gateway_zigbee_stable_input_id(
''',
)

# Track request correlation in reporting state.
replace(
    "main/gateway_device_state.h",
    "    uint8_t last_status;\n    uint32_t requested_at_ms;\n} reporting_state_t;",
    "    uint8_t last_status;\n    uint32_t requested_at_ms;\n    uint32_t request_id;\n    bool request_clamped;\n} reporting_state_t;",
)

# Internal normalized config result metadata.
replace(
    "main/gateway_events.h",
    "typedef struct {\n    uint16_t cluster_id;\n    uint16_t attribute_id;",
    "typedef enum {\n    GATEWAY_EVENT_CONFIG_NONE = 0,\n    GATEWAY_EVENT_CONFIG_APPLIED,\n    GATEWAY_EVENT_CONFIG_CLAMPED,\n    GATEWAY_EVENT_CONFIG_UNSUPPORTED,\n    GATEWAY_EVENT_CONFIG_ERROR,\n} gateway_event_config_result_t;\n\ntypedef struct {\n    uint16_t cluster_id;\n    uint16_t attribute_id;",
)
replace(
    "main/gateway_events.h",
    "        struct {\n            uint16_t cluster_id;\n            uint16_t attribute_id;\n            uint8_t status;\n        } reporting;",
    "        struct {\n            uint16_t cluster_id;\n            uint16_t attribute_id;\n            uint8_t status;\n            uint32_t request_id;\n            gateway_event_config_result_t result;\n        } reporting;",
)

# GatewayLink adapter emits normalized CONFIG_RESULT only for correlated external requests.
replace(
    "main/gateway_link_event_adapter.c",
    "    if (event->kind == GATEWAY_EVENT_MEASUREMENT) {",
    r'''    if (event->kind == GATEWAY_EVENT_REPORTING_CONFIG &&
        event->data.reporting.request_id != 0U &&
        event->data.reporting.result != GATEWAY_EVENT_CONFIG_NONE) {
        gateway_link_config_status_t status = GATEWAY_LINK_CONFIG_ERROR;
        switch (event->data.reporting.result) {
        case GATEWAY_EVENT_CONFIG_APPLIED:
            status = GATEWAY_LINK_CONFIG_APPLIED;
            break;
        case GATEWAY_EVENT_CONFIG_CLAMPED:
            status = GATEWAY_LINK_CONFIG_CLAMPED;
            break;
        case GATEWAY_EVENT_CONFIG_UNSUPPORTED:
            status = GATEWAY_LINK_CONFIG_UNSUPPORTED;
            break;
        case GATEWAY_EVENT_CONFIG_ERROR:
        case GATEWAY_EVENT_CONFIG_NONE:
        default:
            status = GATEWAY_LINK_CONFIG_ERROR;
            break;
        }
        message->type = GATEWAY_LINK_MSG_CONFIG_RESULT;
        const gateway_link_config_result_t result = {
            .request_id = event->data.reporting.request_id,
            .status = status,
        };
        return gateway_link_encode_config_result_payload(
            &result, message->payload, sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    if (event->kind == GATEWAY_EVENT_MEASUREMENT) {''',
)

# Control parser now retains the full policy request.
replace(
    "main/gateway_link_control.h",
    "    GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED,",
    "    GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY,",
)
replace(
    "main/gateway_link_control.h",
    "    uint8_t permit_join_seconds;\n    gateway_link_hello_t peer_hello;",
    "    uint8_t permit_join_seconds;\n    gateway_link_hello_t peer_hello;\n    gateway_link_measurement_policy_t measurement_policy;",
)
replace(
    "main/gateway_link_control.c",
    "        action.kind = GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED;\n        action.request_id = policy.request_id;",
    "        action.kind = GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY;\n        action.request_id = policy.request_id;\n        action.measurement_policy = policy;",
)
replace(
    "main/gateway_link_control.c",
    "        .features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN |\n            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,",
    "        .features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |\n            GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,",
)

# Public Zigbee policy submission API is queue-only; registry mutation remains in discovery task.
Path("main/zigbee_gateway.h").write_text(r'''#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gateway_inputs.h"

typedef enum {
    ZIGBEE_GATEWAY_POLICY_QUEUED = 0,
    ZIGBEE_GATEWAY_POLICY_UNSUPPORTED,
    ZIGBEE_GATEWAY_POLICY_ERROR,
} zigbee_gateway_policy_submit_result_t;

esp_err_t zigbee_gateway_start(void);
esp_err_t zigbee_gateway_set_permit_join(uint8_t seconds);
zigbee_gateway_policy_submit_result_t zigbee_gateway_set_measurement_policy(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    uint32_t min_interval_ms,
    uint32_t max_interval_ms,
    double reportable_change);
''')

# Extend discovery jobs with external policy payload while preserving ordinary discovery behavior.
replace(
    "main/zigbee_gateway.c",
    "    DISCOVERY_CONFIG_REPORTING,\n} discovery_kind_t;",
    "    DISCOVERY_CONFIG_REPORTING,\n    DISCOVERY_EXTERNAL_REPORTING,\n} discovery_kind_t;",
)
replace(
    "main/zigbee_gateway.c",
    "    uint16_t cluster_id;\n    uint8_t retry_count;\n} discovery_job_t;",
    "    uint16_t cluster_id;\n    uint8_t retry_count;\n    bool reporting_spec_valid;\n    gateway_reporting_spec_t reporting_spec;\n    uint32_t config_request_id;\n    bool config_clamped;\n    gateway_input_id_t external_input;\n    gateway_measurement_kind_t external_kind;\n} discovery_job_t;",
)

# Queue helper preserves custom reporting payload across retries.
replace_between(
    "main/zigbee_gateway.c",
    "static bool queue_job(\n    discovery_kind_t kind,\n    device_slot_t *slot,\n    uint8_t endpoint,\n    uint16_t cluster,\n    uint8_t retries)\n{",
    "static bool schedule_basic(device_slot_t *slot, uint8_t endpoint)\n",
    r'''static bool enqueue_device_job(device_slot_t *slot, discovery_job_t job)
{
    if (s_discovery_queue == NULL || slot == NULL || slot->state != SLOT_ACTIVE ||
        slot->device.short_addr == GATEWAY_INVALID_SHORT_ADDR) {
        return false;
    }
    job.device = gateway_device_ref_for(slot);
    job.route_short_addr = slot->device.short_addr;
    if (xQueueSend(s_discovery_queue, &job, 0) != pdPASS) {
        return false;
    }
    ++slot->pending_jobs;
    return true;
}

static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries)
{
    return enqueue_device_job(slot, (discovery_job_t){
        .kind = kind,
        .endpoint = endpoint,
        .cluster_id = cluster,
        .retry_count = retries,
    });
}

static bool queue_reporting_job(
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    const gateway_reporting_spec_t *spec,
    uint32_t request_id,
    bool clamped,
    uint8_t retries)
{
    if (spec == NULL) {
        return false;
    }
    return enqueue_device_job(slot, (discovery_job_t){
        .kind = DISCOVERY_CONFIG_REPORTING,
        .endpoint = endpoint,
        .cluster_id = cluster,
        .retry_count = retries,
        .reporting_spec_valid = true,
        .reporting_spec = *spec,
        .config_request_id = request_id,
        .config_clamped = clamped,
    });
}

static bool schedule_basic(device_slot_t *slot, uint8_t endpoint)
''',
)

# Auto reporting jobs also carry an explicit spec.
replace(
    "main/zigbee_gateway.c",
    "    if (!queue_job(DISCOVERY_CONFIG_REPORTING, slot, endpoint, cluster, 0U)) {\n        gateway_event_warning(&slot->device, \"reporting queue full\");\n        return false;\n    }",
    "    if (!queue_reporting_job(slot, endpoint, cluster, &spec, 0U, false, 0U)) {\n        gateway_event_warning(&slot->device, \"reporting queue full\");\n        return false;\n    }",
)

# Clear and retry preserve exact custom specs/request correlation.
replace(
    "main/zigbee_gateway.c",
    "    if (job->kind == DISCOVERY_CONFIG_REPORTING) {\n        gateway_reporting_spec_t spec;\n        if (gateway_reporting_policy_spec(job->cluster_id, &spec)) {\n            reporting_state_t *reporting = gateway_device_reporting_state(\n                slot, job->endpoint, job->cluster_id, spec.attribute_id, false);\n            if (reporting != NULL) {\n                reporting->requested = false;\n            }\n        }\n    }",
    "    if (job->kind == DISCOVERY_CONFIG_REPORTING && job->reporting_spec_valid) {\n        reporting_state_t *reporting = gateway_device_reporting_state(\n            slot, job->endpoint, job->cluster_id,\n            job->reporting_spec.attribute_id, false);\n        if (reporting != NULL) {\n            reporting->requested = false;\n        }\n    }",
)
replace(
    "main/zigbee_gateway.c",
    "        queue_job(\n            job.kind,\n            slot,\n            job.endpoint,\n            job.cluster_id,\n            (uint8_t)(job.retry_count + 1U))) {",
    "        enqueue_device_job(\n            slot,\n            (discovery_job_t){\n                .kind = job.kind,\n                .endpoint = job.endpoint,\n                .cluster_id = job.cluster_id,\n                .retry_count = (uint8_t)(job.retry_count + 1U),\n                .reporting_spec_valid = job.reporting_spec_valid,\n                .reporting_spec = job.reporting_spec,\n                .config_request_id = job.config_request_id,\n                .config_clamped = job.config_clamped,\n            })) {",
)

# Helper emits a correlated normalized result without exposing ZCL semantics to GatewayLink.
replace(
    "main/zigbee_gateway.c",
    "static void retry_or_fail(discovery_job_t job, const char *message)\n{",
    r'''static void publish_reporting_config_event(
    const gateway_device_id_t *device,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint8_t zcl_status,
    uint32_t request_id,
    gateway_event_config_result_t result)
{
    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_REPORTING_CONFIG, device);
    event.endpoint = endpoint;
    event.data.reporting.cluster_id = cluster_id;
    event.data.reporting.attribute_id = attribute_id;
    event.data.reporting.status = zcl_status;
    event.data.reporting.request_id = request_id;
    event.data.reporting.result = result;
    gateway_event_publish(&event);
}

static void fail_external_reporting_job(
    device_slot_t *slot, const discovery_job_t *job, uint8_t status)
{
    if (job == NULL || job->config_request_id == 0U || !job->reporting_spec_valid) {
        return;
    }
    if (slot != NULL) {
        reporting_state_t *reporting = gateway_device_reporting_state(
            slot, job->endpoint, job->cluster_id,
            job->reporting_spec.attribute_id, false);
        if (reporting != NULL && reporting->request_id == job->config_request_id) {
            reporting->requested = false;
            reporting->request_id = 0U;
            reporting->request_clamped = false;
        }
    }
    publish_reporting_config_event(
        slot == NULL ? NULL : &slot->device,
        job->endpoint, job->cluster_id, job->reporting_spec.attribute_id,
        status, job->config_request_id, GATEWAY_EVENT_CONFIG_ERROR);
}

static void retry_or_fail(discovery_job_t job, const char *message)
{''',
)
replace(
    "main/zigbee_gateway.c",
    "    clear_pending(slot, &job);\n    if (job.kind == DISCOVERY_ACTIVE_ENDPOINTS) {",
    "    clear_pending(slot, &job);\n    if (job.kind == DISCOVERY_CONFIG_REPORTING) {\n        fail_external_reporting_job(slot, &job, GATEWAY_CONFIG_STATUS_UNKNOWN);\n    }\n    if (job.kind == DISCOVERY_ACTIVE_ENDPOINTS) {",
)

# Configure Reporting response now correlates request id and reports APPLIED/CLAMPED/ERROR.
replace_between(
    "main/zigbee_gateway.c",
    "static void publish_config_response(\n",
    "static void handle_check_in(ezb_zcl_poll_control_check_in_message_t *message)\n",
    r'''static void publish_config_response(
    const ezb_zcl_cmd_config_report_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT) {
        return;
    }
    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = gateway_device_find_by_short(device.short_addr, false);

    for (ezb_zcl_config_report_rsp_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        reporting_state_t *reporting = slot == NULL ? NULL :
            gateway_device_reporting_state(
                slot, header->src_ep, header->cluster_id, item->attr_id, false);
        uint32_t request_id = 0U;
        bool request_clamped = false;
        if (reporting != NULL) {
            request_id = reporting->request_id;
            request_clamped = reporting->request_clamped;
            reporting->requested = false;
            reporting->last_status = item->status;
            if (item->status == EZB_ZCL_STATUS_SUCCESS) {
                reporting->configured = true;
            }
            reporting->request_id = 0U;
            reporting->request_clamped = false;
        }
        const gateway_event_config_result_t result =
            item->status == EZB_ZCL_STATUS_SUCCESS ?
                (request_clamped ? GATEWAY_EVENT_CONFIG_CLAMPED :
                                   GATEWAY_EVENT_CONFIG_APPLIED) :
                GATEWAY_EVENT_CONFIG_ERROR;
        publish_reporting_config_event(
            &device, header->src_ep, header->cluster_id, item->attr_id,
            item->status, request_id, result);
    }
}

static void handle_check_in(ezb_zcl_poll_control_check_in_message_t *message)
''',
)

# A stale externally requested Configure Reporting operation must complete with ERROR, not hang forever.
replace(
    "main/zigbee_gateway.c",
    "        if (reporting->in_use && reporting->requested &&\n            (uint32_t)(now_ms - reporting->requested_at_ms) >=\n                GATEWAY_REQUEST_STALE_MS) {\n            reporting->requested = false;\n        }",
    "        if (reporting->in_use && reporting->requested &&\n            (uint32_t)(now_ms - reporting->requested_at_ms) >=\n                GATEWAY_REQUEST_STALE_MS) {\n            if (reporting->request_id != 0U) {\n                publish_reporting_config_event(\n                    &slot->device, reporting->endpoint, reporting->cluster_id,\n                    reporting->attribute_id, GATEWAY_CONFIG_STATUS_UNKNOWN,\n                    reporting->request_id, GATEWAY_EVENT_CONFIG_ERROR);\n            }\n            reporting->requested = false;\n            reporting->request_id = 0U;\n            reporting->request_clamped = false;\n        }",
)

# Config submit uses job-carried exact spec.
replace_between(
    "main/zigbee_gateway.c",
    "static bool submit_reporting(device_slot_t *slot, const discovery_job_t *job)\n",
    "static void discovery_task(void *arg)\n",
    r'''static bool submit_reporting(device_slot_t *slot, const discovery_job_t *job)
{
    if (job == NULL || !job->reporting_spec_valid) {
        return false;
    }
    const gateway_reporting_spec_t *spec = &job->reporting_spec;

    ezb_zcl_config_report_record_t record = {
        .direction = EZB_ZCL_REPORTING_SEND,
        .attr_id = spec->attribute_id,
    };
    record.client.attr_type = spec->attribute_type;
    record.client.min_interval = spec->min_interval;
    record.client.max_interval = spec->max_interval;
    switch (spec->change_kind) {
    case GATEWAY_REPORTING_CHANGE_S16:
        record.client.reportable_change.s16 = (int16_t)spec->reportable_change;
        break;
    case GATEWAY_REPORTING_CHANGE_U16:
        record.client.reportable_change.u16 = (uint16_t)spec->reportable_change;
        break;
    case GATEWAY_REPORTING_CHANGE_U8:
        record.client.reportable_change.u8 = (uint8_t)spec->reportable_change;
        break;
    default:
        return false;
    }

    const ezb_zcl_config_report_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = job->endpoint,
            .src_ep = GATEWAY_ENDPOINT,
            .cluster_id = job->cluster_id,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.record_number = 1U, .record_field = &record},
    };
    return ezb_zcl_config_report_cmd_req(&request) == EZB_ERR_NONE;
}

static void reject_external_reporting(
    const discovery_job_t *job, gateway_event_config_result_t result)
{
    if (job == NULL || job->config_request_id == 0U) {
        return;
    }
    publish_reporting_config_event(
        NULL, job->external_input.channel, job->cluster_id,
        job->reporting_spec.attribute_id, GATEWAY_CONFIG_STATUS_UNKNOWN,
        job->config_request_id, result);
}

static void handle_external_reporting(const discovery_job_t *external)
{
    uint8_t ieee[8];
    uint8_t endpoint = 0U;
    if (external == NULL || !external->reporting_spec_valid ||
        !gateway_zigbee_parse_input_identity(&external->external_input, ieee, &endpoint)) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    device_slot_t *slot = gateway_device_find_by_ieee(ieee, false);
    endpoint_state_t *ep_state = slot == NULL ? NULL : endpoint_state(slot, endpoint, false);
    const gateway_input_capabilities_t capability =
        gateway_input_capability_for_measurement(external->external_kind);
    if (slot == NULL || ep_state == NULL) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    if (capability == 0U || (ep_state->input_profile.configurable & capability) == 0U) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_UNSUPPORTED);
        return;
    }
    if (gateway_reporting_policy_requires_binding(external->cluster_id)) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, endpoint, external->cluster_id, false);
        if (binding == NULL || !binding->configured) {
            reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
            return;
        }
    }
    reporting_state_t *reporting = gateway_device_reporting_state(
        slot, endpoint, external->cluster_id,
        external->reporting_spec.attribute_id, true);
    if (reporting == NULL || reporting->requested) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    if (!queue_reporting_job(
            slot, endpoint, external->cluster_id, &external->reporting_spec,
            external->config_request_id, external->config_clamped, 0U)) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    reporting->requested = true;
    reporting->requested_at_ms = gateway_uptime_ms();
    reporting->last_status = GATEWAY_CONFIG_STATUS_UNKNOWN;
    reporting->request_id = external->config_request_id;
    reporting->request_clamped = external->config_clamped;
}

static void discovery_task(void *arg)
''',
)

# Discovery task resolves external stable identity inside its single-owner state context.
replace(
    "main/zigbee_gateway.c",
    "        if (xQueueReceive(s_discovery_queue, &job, portMAX_DELAY) != pdPASS) {\n            continue;\n        }\n        device_slot_t *slot = gateway_device_from_ref(job.device, true);",
    "        if (xQueueReceive(s_discovery_queue, &job, portMAX_DELAY) != pdPASS) {\n            continue;\n        }\n        if (job.kind == DISCOVERY_EXTERNAL_REPORTING) {\n            handle_external_reporting(&job);\n            continue;\n        }\n        device_slot_t *slot = gateway_device_from_ref(job.device, true);",
)
replace(
    "main/zigbee_gateway.c",
    "            clear_pending(slot, &job);\n            gateway_event_warning(&slot->device, \"discovery cancelled: route superseded\");",
    "            clear_pending(slot, &job);\n            if (job.kind == DISCOVERY_CONFIG_REPORTING) {\n                fail_external_reporting_job(slot, &job, GATEWAY_CONFIG_STATUS_UNKNOWN);\n            }\n            gateway_event_warning(&slot->device, \"discovery cancelled: route superseded\");",
)

# Public queue submission appended before permit-join API.
replace(
    "main/zigbee_gateway.c",
    "esp_err_t zigbee_gateway_set_permit_join(uint8_t seconds)\n{",
    r'''zigbee_gateway_policy_submit_result_t zigbee_gateway_set_measurement_policy(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    uint32_t min_interval_ms,
    uint32_t max_interval_ms,
    double reportable_change)
{
    if (request_id == 0U || input == NULL || input->source != GATEWAY_SOURCE_ZIGBEE) {
        return ZIGBEE_GATEWAY_POLICY_UNSUPPORTED;
    }
    gateway_reporting_plan_t plan;
    const gateway_reporting_plan_result_t plan_result = gateway_reporting_policy_plan(
        kind, min_interval_ms, max_interval_ms, reportable_change, &plan);
    if (plan_result == GATEWAY_REPORTING_PLAN_UNSUPPORTED) {
        return ZIGBEE_GATEWAY_POLICY_UNSUPPORTED;
    }
    if (plan_result == GATEWAY_REPORTING_PLAN_INVALID || !stack_is_ready() ||
        s_discovery_queue == NULL) {
        return ZIGBEE_GATEWAY_POLICY_ERROR;
    }
    const discovery_job_t job = {
        .kind = DISCOVERY_EXTERNAL_REPORTING,
        .cluster_id = plan.cluster_id,
        .reporting_spec_valid = true,
        .reporting_spec = plan.spec,
        .config_request_id = request_id,
        .config_clamped = plan.clamped,
        .external_input = *input,
        .external_kind = kind,
    };
    return xQueueSend(s_discovery_queue, &job, 0U) == pdPASS ?
        ZIGBEE_GATEWAY_POLICY_QUEUED : ZIGBEE_GATEWAY_POLICY_ERROR;
}

esp_err_t zigbee_gateway_set_permit_join(uint8_t seconds)
{''',
)

# GatewayLink runtime submits Zigbee policy and waits for correlated event result if queued.
replace(
    "main/gateway_link.c",
    "    case GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED:\n        if (gateway_link_make_config_result_message(\n                action.request_id, GATEWAY_LINK_CONFIG_UNSUPPORTED, &response)) {\n            (void)enqueue_message(&response);\n        }\n        break;",
    r'''    case GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY:
    {
        const gateway_link_measurement_policy_t *policy = &action.measurement_policy;
        const zigbee_gateway_policy_submit_result_t result =
            zigbee_gateway_set_measurement_policy(
                policy->request_id, &policy->input, policy->kind,
                policy->min_interval_ms, policy->max_interval_ms,
                policy->reportable_change);
        if (result == ZIGBEE_GATEWAY_POLICY_QUEUED) {
            break;
        }
        const gateway_link_config_status_t status =
            result == ZIGBEE_GATEWAY_POLICY_UNSUPPORTED ?
                GATEWAY_LINK_CONFIG_UNSUPPORTED : GATEWAY_LINK_CONFIG_ERROR;
        if (gateway_link_make_config_result_message(policy->request_id, status, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    }''',
)

# Reporting diagnostics retain raw ZCL status plus normalized request result.
replace(
    "main/gateway_transport.c",
    "        ESP_LOGI(TAG, \"reporting %s ep=%u cluster=0x%04x attr=0x%04x status=0x%02x\",\n                 device, event->endpoint, event->data.reporting.cluster_id,\n                 event->data.reporting.attribute_id, event->data.reporting.status);",
    "        ESP_LOGI(TAG, \"reporting %s ep=%u cluster=0x%04x attr=0x%04x status=0x%02x request=%\" PRIu32 \" result=%u\",\n                 device, event->endpoint, event->data.reporting.cluster_id,\n                 event->data.reporting.attribute_id, event->data.reporting.status,\n                 event->data.reporting.request_id, (unsigned)event->data.reporting.result);",
)

# Host tests: policy translation.
Path("tests/host/test_gateway_reporting_policy.c").write_text(r'''#include <assert.h>
#include <stdio.h>

#include "gateway_reporting_policy.h"

#define CLUSTER_POWER_CONFIG 0x0001U
#define CLUSTER_POLL_CONTROL 0x0020U
#define CLUSTER_TEMPERATURE 0x0402U
#define CLUSTER_HUMIDITY 0x0405U

static void test_default_policy(void)
{
    gateway_reporting_spec_t spec;
    assert(gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, &spec));
    assert(spec.attribute_id == 0x0000U);
    assert(spec.attribute_type == 0x29U);
    assert(spec.min_interval == 60U);
    assert(spec.max_interval == 300U);
    assert(spec.change_kind == GATEWAY_REPORTING_CHANGE_S16);
    assert(spec.reportable_change == 10);

    assert(gateway_reporting_policy_spec(CLUSTER_HUMIDITY, &spec));
    assert(spec.attribute_type == 0x21U);
    assert(spec.reportable_change == 100);

    assert(gateway_reporting_policy_spec(CLUSTER_POWER_CONFIG, &spec));
    assert(spec.attribute_id == 0x0021U);
    assert(spec.attribute_type == 0x20U);
    assert(spec.min_interval == 3600U);
    assert(spec.max_interval == 21600U);
    assert(spec.change_kind == GATEWAY_REPORTING_CHANGE_U8);
    assert(spec.reportable_change == 2);

    assert(gateway_reporting_policy_requires_binding(CLUSTER_TEMPERATURE));
    assert(gateway_reporting_policy_requires_binding(CLUSTER_POLL_CONTROL));
    assert(!gateway_reporting_policy_requires_binding(0xffffU));
    assert(!gateway_reporting_policy_spec(0xffffU, &spec));
    assert(!gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, NULL));
}

static void test_requested_policy_translation(void)
{
    gateway_reporting_plan_t plan;
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 60000U, 300000U, 0.10, &plan) ==
        GATEWAY_REPORTING_PLAN_OK);
    assert(plan.cluster_id == CLUSTER_TEMPERATURE);
    assert(plan.spec.min_interval == 60U && plan.spec.max_interval == 300U);
    assert(plan.spec.reportable_change == 10);
    assert(plan.effective_reportable_change == 0.10);
    assert(!plan.clamped);

    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_HUMIDITY, 60500U, 300900U, 1.005, &plan) ==
        GATEWAY_REPORTING_PLAN_CLAMPED);
    assert(plan.spec.min_interval == 61U);
    assert(plan.spec.max_interval == 300U);
    assert(plan.spec.reportable_change == 101);
    assert(plan.effective_min_interval_ms == 61000U);
    assert(plan.effective_max_interval_ms == 300000U);
    assert(plan.effective_reportable_change == 1.01);
    assert(plan.clamped);

    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_BATTERY_PERCENT, 3600000U, 21600000U, 1.0, &plan) ==
        GATEWAY_REPORTING_PLAN_OK);
    assert(plan.cluster_id == CLUSTER_POWER_CONFIG);
    assert(plan.spec.reportable_change == 2);

    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_CO2, 1000U, 5000U, 1.0, &plan) ==
        GATEWAY_REPORTING_PLAN_UNSUPPORTED);
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 5000U, 1000U, 0.1, &plan) ==
        GATEWAY_REPORTING_PLAN_INVALID);
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 1000U, 0U, 0.1, &plan) ==
        GATEWAY_REPORTING_PLAN_INVALID);
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 1000U, 5000U, -0.1, &plan) ==
        GATEWAY_REPORTING_PLAN_INVALID);
}

int main(void)
{
    test_default_policy();
    test_requested_policy_translation();
    puts("gateway_reporting_policy host tests passed");
    return 0;
}
''')

# Zigbee identity parse round-trip and malformed cases.
replace(
    "tests/host/test_gateway_zigbee_input.c",
    "    assert(strcmp(input.id, \"zigbee:00124b00aabbccdd\") == 0);\n}",
    r'''    assert(strcmp(input.id, "zigbee:00124b00aabbccdd") == 0);

    uint8_t parsed_ieee[8] = {0};
    uint8_t endpoint = 0U;
    assert(gateway_zigbee_parse_input_identity(&input, parsed_ieee, &endpoint));
    assert(memcmp(parsed_ieee, ieee, sizeof(ieee)) == 0);
    assert(endpoint == 7U);

    input.id[8] = 'x';
    assert(!gateway_zigbee_parse_input_identity(&input, parsed_ieee, &endpoint));
    input = gateway_input_make(GATEWAY_SOURCE_LOCAL_I2C, "zigbee:00124b00aabbccdd", 7U);
    assert(!gateway_zigbee_parse_input_identity(&input, parsed_ieee, &endpoint));
}''',
)

# Control parser tests now preserve policy instead of declaring it unsupported.
replace(
    "tests/host/test_gateway_link_control.c",
    "static void test_measurement_policy_truthfully_unsupported(void)",
    "static void test_measurement_policy_request(void)",
)
replace(
    "tests/host/test_gateway_link_control.c",
    "    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED);\n    assert(action.request_id == 77U);",
    "    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY);\n    assert(action.request_id == 77U);\n    assert(action.measurement_policy.request_id == 77U);\n    assert(action.measurement_policy.kind == GATEWAY_MEAS_TEMPERATURE);\n    assert(action.measurement_policy.min_interval_ms == 5000U);\n    assert(action.measurement_policy.max_interval_ms == 60000U);",
)
replace(
    "tests/host/test_gateway_link_control.c",
    "    test_measurement_policy_truthfully_unsupported();",
    "    test_measurement_policy_request();",
)
replace(
    "tests/host/test_gateway_link_control.c",
    "GATEWAY_LINK_FEATURE_SNAPSHOT |\n        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE",
    "GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |\n        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE",
    2,
)

# Event adapter tests correlated reporting CONFIG_RESULT mapping.
replace(
    "tests/host/test_gateway_link_event_adapter.c",
    "static void test_protocol_specific_event_is_not_forwarded(void)",
    r'''static void test_reporting_config_result(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_REPORTING_CONFIG;
    event.data.reporting.request_id = 91U;
    event.data.reporting.result = GATEWAY_EVENT_CONFIG_CLAMPED;
    gateway_link_message_t message;
    assert(gateway_link_message_from_event(&event, &message));
    assert(message.type == GATEWAY_LINK_MSG_CONFIG_RESULT);
    gateway_link_config_result_t decoded = {0};
    assert(gateway_link_decode_config_result_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 91U);
    assert(decoded.status == GATEWAY_LINK_CONFIG_CLAMPED);

    event.data.reporting.request_id = 0U;
    assert(!gateway_link_message_from_event(&event, &message));
}

static void test_protocol_specific_event_is_not_forwarded(void)''',
)
replace(
    "tests/host/test_gateway_link_event_adapter.c",
    "    test_measurement_event();\n    test_protocol_specific_event_is_not_forwarded();",
    "    test_measurement_event();\n    test_reporting_config_result();\n    test_protocol_specific_event_is_not_forwarded();",
)

# Virtual-S3 E2E now validates policy request parsing; runtime completion is event-driven on firmware.
replace(
    "tests/host/test_gateway_link_e2e.c",
    "static void test_snapshot_replay_and_unsupported_controls(void)",
    "static void test_snapshot_replay_and_policy_control(void)",
)
replace(
    "tests/host/test_gateway_link_e2e.c",
    "    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED);\n    assert(action.request_id == 123U);\n\n    gateway_link_message_t response;\n    assert(gateway_link_make_config_result_message(\n        action.request_id, GATEWAY_LINK_CONFIG_UNSUPPORTED, &response));\n    gateway_link_config_result_t decoded_result = {0};\n    assert(gateway_link_decode_config_result_payload(\n        response.payload, response.payload_length, &decoded_result) == GATEWAY_LINK_OK);\n    assert(decoded_result.request_id == 123U);\n    assert(decoded_result.status == GATEWAY_LINK_CONFIG_UNSUPPORTED);",
    "    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY);\n    assert(action.request_id == 123U);\n    assert(action.measurement_policy.kind == GATEWAY_MEAS_TEMPERATURE);\n    assert(action.measurement_policy.min_interval_ms == 1000U);\n    assert(action.measurement_policy.max_interval_ms == 5000U);",
)
replace(
    "tests/host/test_gateway_link_e2e.c",
    "    test_snapshot_replay_and_unsupported_controls();",
    "    test_snapshot_replay_and_policy_control();",
)
replace(
    "tests/host/test_gateway_link_e2e.c",
    "    assert((c6_hello.features & GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE) != 0U);\n    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) == 0U);",
    "    assert((c6_hello.features & GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE) != 0U);\n    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) != 0U);",
)

# Documentation reflects actual asynchronous policy semantics.
replace(
    "docs/GATEWAY_LINK_V2.md",
    "HELLO advertises the `snapshot`, `permit-join` and `capability-profile` feature bits. `measurement-policy` remains unadvertised until the next phase connects the request to real Zigbee Configure Reporting state.",
    "HELLO advertises `snapshot`, `measurement-policy`, `permit-join` and `capability-profile`. For supported Zigbee inputs, `SET_MEASUREMENT_POLICY` is translated into a standard Configure Reporting request. The C6 returns `CONFIG_RESULT` only after the device response, or an explicit normalized error/unsupported result if the request cannot be applied. Interval quantization or reportable-change quantization is returned as `CLAMPED` after a successful device response.",
)
replace(
    "docs/GATEWAY_LINK_V2.md",
    "MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload semantics remain otherwise unchanged from v1. A peer must negotiate protocol version 2; the active branch intentionally does not decode v1 frames.",
    "MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload encodings remain otherwise unchanged from v1. Supported measurement-policy targets are currently temperature, relative humidity and battery percentage, matching the standard reporting policy table. Requests are routed by authoritative Zigbee IEEE input identity plus endpoint; the GatewayLink RX task never uses a mutable short address as application identity. A peer must negotiate protocol version 2; the active branch intentionally does not decode v1 frames.",
)
replace(
    "docs/CONTINUATION.md",
    "- The next implementation slice connects `SET_MEASUREMENT_POLICY` to real Zigbee Configure Reporting and returns normalized per-request results, followed by writable On/Off and Level commands.",
    "- Phase 4 connects `SET_MEASUREMENT_POLICY` to standard Zigbee Configure Reporting through the discovery-task ownership boundary. Supported requests are correlated by `request_id`; APPLIED/CLAMPED/UNSUPPORTED/ERROR is emitted only from real validation/device-response outcomes.\n- The next implementation slice adds normalized writable On/Off first, then Level Control, before building the second-C6 emulator profiles for deterministic round-trip testing.",
)
with Path("docs/ARCHITECTURE.md").open("a") as f:
    f.write(r'''

## Measurement policy command ownership

GatewayLink RX validates and enqueues source-neutral measurement policy requests but does not mutate the Zigbee registry. The Zigbee discovery task resolves the stable IEEE+endpoint identity, validates the endpoint's configurable capability, checks binding state and owns Configure Reporting submission. A correlated request completes only after the ZCL Configure Reporting response (or an explicit timeout/queue/route failure), which is normalized to `APPLIED`, `CLAMPED`, `UNSUPPORTED` or `ERROR` before crossing GatewayLink.
''')

print("phase4 patch prepared")
