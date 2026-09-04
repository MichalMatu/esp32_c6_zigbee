#!/usr/bin/env bash
set -euo pipefail

BRANCH=integration/c6-s3-i2c-20260903
BASE=8badcdece00c876e5281afb3ca82a846859fab2c

git fetch origin
git checkout "$BRANCH"
git reset --hard "origin/$BRANCH"
[ "$(git rev-parse HEAD)" = "$BASE" ]

python3 - <<'PY'
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f'marker missing in {path}: {old[:120]!r}')
    p.write_text(text.replace(old, new, 1))

# Normalized measurement/capability surface.
replace_once('main/gateway_inputs.h',
'''    GATEWAY_MEAS_ON_OFF,\n    GATEWAY_MEAS_LEVEL,\n} gateway_measurement_kind_t;''',
'''    GATEWAY_MEAS_ON_OFF,\n    GATEWAY_MEAS_LEVEL,\n    GATEWAY_MEAS_CONTACT_OPEN,\n} gateway_measurement_kind_t;''')
replace_once('main/gateway_inputs.h',
'''#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)\n#define GATEWAY_INPUT_CAP_LEVEL           (1UL << 13)''',
'''#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)\n#define GATEWAY_INPUT_CAP_LEVEL           (1UL << 13)\n#define GATEWAY_INPUT_CAP_CONTACT_OPEN    (1UL << 14)''')
replace_once('main/gateway_inputs.c',
'''        GATEWAY_INPUT_CAP_ON_OFF,\n        GATEWAY_INPUT_CAP_LEVEL,\n    };''',
'''        GATEWAY_INPUT_CAP_ON_OFF,\n        GATEWAY_INPUT_CAP_LEVEL,\n        GATEWAY_INPUT_CAP_CONTACT_OPEN,\n    };''')

# GatewayLink v2 wire kind 15.
replace_once('main/gateway_link_protocol.c',
'''    case GATEWAY_MEAS_ON_OFF: *wire = 13U; break;\n    case GATEWAY_MEAS_LEVEL: *wire = 14U; break;''',
'''    case GATEWAY_MEAS_ON_OFF: *wire = 13U; break;\n    case GATEWAY_MEAS_LEVEL: *wire = 14U; break;\n    case GATEWAY_MEAS_CONTACT_OPEN: *wire = 15U; break;''')
replace_once('main/gateway_link_protocol.c',
'''    case 13U: *kind = GATEWAY_MEAS_ON_OFF; break;\n    case 14U: *kind = GATEWAY_MEAS_LEVEL; break;''',
'''    case 13U: *kind = GATEWAY_MEAS_ON_OFF; break;\n    case 14U: *kind = GATEWAY_MEAS_LEVEL; break;\n    case 15U: *kind = GATEWAY_MEAS_CONTACT_OPEN; break;''')

# Pure type-gated IAS contact normalizer.
replace_once('main/gateway_zcl_value.h',
'''bool gateway_zcl_normalize(uint16_t cluster,\n                           uint16_t attribute,\n                           uint8_t type,\n                           const void *value,\n                           gateway_measurement_kind_t *kind,\n                           gateway_unit_t *unit,\n                           double *number);''',
'''bool gateway_zcl_normalize(uint16_t cluster,\n                           uint16_t attribute,\n                           uint8_t type,\n                           const void *value,\n                           gateway_measurement_kind_t *kind,\n                           gateway_unit_t *unit,\n                           double *number);\n\nbool gateway_zcl_normalize_ias_contact(\n    uint16_t zone_type, uint16_t zone_status,\n    gateway_measurement_kind_t *kind,\n    gateway_unit_t *unit,\n    double *number);''')
replace_once('main/gateway_zcl_value.c',
'''#define ZCL_ATTR_BATTERY_PERCENT 0x0021U\n''',
'''#define ZCL_ATTR_BATTERY_PERCENT 0x0021U\n#define ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH 0x0015U\n#define ZCL_IAS_ZONE_STATUS_ALARM1 0x0001U\n''')
replace_once('main/gateway_zcl_value.c',
'''static bool read_float(const void *value, uint8_t type, float *out)\n{\n    if (value == NULL || out == NULL || type != EZB_ZCL_ATTR_TYPE_SINGLE) {\n        return false;\n    }\n    memcpy(out, value, sizeof(*out));\n    return true;\n}\n''',
'''static bool read_float(const void *value, uint8_t type, float *out)\n{\n    if (value == NULL || out == NULL || type != EZB_ZCL_ATTR_TYPE_SINGLE) {\n        return false;\n    }\n    memcpy(out, value, sizeof(*out));\n    return true;\n}\n\nbool gateway_zcl_normalize_ias_contact(\n    uint16_t zone_type, uint16_t zone_status,\n    gateway_measurement_kind_t *kind,\n    gateway_unit_t *unit,\n    double *number)\n{\n    if (kind == NULL || unit == NULL || number == NULL ||\n        zone_type != ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH) {\n        return false;\n    }\n    *kind = GATEWAY_MEAS_CONTACT_OPEN;\n    *unit = GATEWAY_UNIT_BOOLEAN;\n    *number = (zone_status & ZCL_IAS_ZONE_STATUS_ALARM1) != 0U;\n    return true;\n}\n''')

# Endpoint IAS discovery/cache state.
replace_once('main/gateway_device_state.h',
'''    char manufacturer[GATEWAY_BASIC_TEXT_MAX_BYTES];\n    char model[GATEWAY_BASIC_TEXT_MAX_BYTES];\n    bool input_announced;\n} endpoint_state_t;''',
'''    char manufacturer[GATEWAY_BASIC_TEXT_MAX_BYTES];\n    char model[GATEWAY_BASIC_TEXT_MAX_BYTES];\n    bool input_announced;\n    bool ias_zone_type_known;\n    bool ias_zone_type_read_requested;\n    bool ias_zone_status_valid;\n    uint16_t ias_zone_type;\n    uint16_t ias_zone_status;\n    uint32_t ias_zone_type_requested_at_ms;\n} endpoint_state_t;''')

# Zigbee IAS discovery and runtime handling.
replace_once('main/zigbee_gateway.c',
'''#include <ezbee/zcl/cluster/on_off.h>\n#include <ezbee/zcl/cluster/level.h>''',
'''#include <ezbee/zcl/cluster/on_off.h>\n#include <ezbee/zcl/cluster/level.h>\n#include <ezbee/zcl/cluster/ias_zone.h>\n#include <ezbee/zcl/cluster/ias_zone_desc.h>''')
replace_once('main/zigbee_gateway.c',
'''#define ZCL_ATTR_BASIC_MANUFACTURER_NAME 0x0004U\n#define ZCL_ATTR_BASIC_MODEL_IDENTIFIER 0x0005U\n''',
'''#define ZCL_ATTR_BASIC_MANUFACTURER_NAME 0x0004U\n#define ZCL_ATTR_BASIC_MODEL_IDENTIFIER 0x0005U\n#define ZCL_CLUSTER_IAS_ZONE 0x0500U\n#define ZCL_ATTR_IAS_ZONE_TYPE 0x0001U\n#define ZCL_ATTR_IAS_ZONE_STATUS 0x0002U\n#define ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH 0x0015U\n''')
replace_once('main/zigbee_gateway.c',
'''    DISCOVERY_SIMPLE_DESCRIPTOR,\n    DISCOVERY_READ_BASIC,\n    DISCOVERY_BIND_CLUSTER,''',
'''    DISCOVERY_SIMPLE_DESCRIPTOR,\n    DISCOVERY_READ_BASIC,\n    DISCOVERY_READ_IAS_ZONE_TYPE,\n    DISCOVERY_BIND_CLUSTER,''')
replace_once('main/zigbee_gateway.c',
'''static bool schedule_binding(\n    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)''',
'''static bool schedule_ias_zone_type(device_slot_t *slot, uint8_t endpoint)\n{\n    endpoint_state_t *state = endpoint_state(slot, endpoint, true);\n    if (state == NULL || state->ias_zone_type_known ||\n        state->ias_zone_type_read_requested) {\n        return false;\n    }\n    if (!queue_job(\n            DISCOVERY_READ_IAS_ZONE_TYPE, slot, endpoint,\n            ZCL_CLUSTER_IAS_ZONE, 0U)) {\n        gateway_event_warning(&slot->device, "IAS ZoneType read queue full");\n        return false;\n    }\n    state->ias_zone_type_read_requested = true;\n    state->ias_zone_type_requested_at_ms = gateway_uptime_ms();\n    return true;\n}\n\nstatic bool schedule_binding(\n    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)''')
replace_once('main/zigbee_gateway.c',
'''    if (job->kind == DISCOVERY_READ_BASIC && state != NULL &&\n        state->basic_state == BASIC_SCHEDULED) {\n        state->basic_state = BASIC_NOT_SCHEDULED;\n    }\n    if (job->kind == DISCOVERY_BIND_CLUSTER) {''',
'''    if (job->kind == DISCOVERY_READ_BASIC && state != NULL &&\n        state->basic_state == BASIC_SCHEDULED) {\n        state->basic_state = BASIC_NOT_SCHEDULED;\n    }\n    if (job->kind == DISCOVERY_READ_IAS_ZONE_TYPE && state != NULL) {\n        state->ias_zone_type_read_requested = false;\n    }\n    if (job->kind == DISCOVERY_BIND_CLUSTER) {''')

insert = r'''
static bool publish_ias_contact_measurement(
    device_slot_t *slot, endpoint_state_t *state, uint16_t zone_status)
{
    if (slot == NULL || state == NULL || !state->ias_zone_type_known) {
        return false;
    }
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;
    if (!gateway_zcl_normalize_ias_contact(
            state->ias_zone_type, zone_status, &kind, &unit, &value)) {
        return false;
    }
    gateway_input_id_t input = {0};
    if (!gateway_zigbee_stable_input_id(
            slot->device.ieee, slot->device.ieee_valid,
            state->endpoint, &input)) {
        return false;
    }
    gateway_event_t event = gateway_event_make_input(
        GATEWAY_EVENT_MEASUREMENT, &input);
    event.endpoint = state->endpoint;
    event.data.measurement = (gateway_measurement_t){
        .kind = kind,
        .unit = unit,
        .value = value,
    };
    return gateway_event_publish(&event);
}

static void apply_ias_zone_type(
    device_slot_t *slot, endpoint_state_t *state, uint16_t zone_type)
{
    if (slot == NULL || state == NULL) {
        return;
    }
    const gateway_input_capabilities_t old_readable = state->input_profile.readable;
    gateway_input_capabilities_t new_readable =
        old_readable & ~GATEWAY_INPUT_CAP_CONTACT_OPEN;
    if (zone_type == ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH) {
        new_readable |= GATEWAY_INPUT_CAP_CONTACT_OPEN;
    }

    if (state->input_announced && old_readable != 0U && new_readable == 0U) {
        (void)publish_generic_input(slot, state, false);
    }
    state->ias_zone_type = zone_type;
    state->ias_zone_type_known = true;
    state->ias_zone_type_read_requested = false;
    state->input_profile.readable = new_readable;

    if (new_readable != 0U &&
        (!state->input_announced || new_readable != old_readable)) {
        (void)publish_generic_input(slot, state, true);
    }
    if (state->ias_zone_status_valid) {
        (void)publish_ias_contact_measurement(
            slot, state, state->ias_zone_status);
    }
}

static void publish_ias_zone_type_read(
    const ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT ||
        header->cluster_id != ZCL_CLUSTER_IAS_ZONE) {
        return;
    }
    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = device.ieee_valid ?
        gateway_device_find_by_ieee(device.ieee, false) :
        gateway_device_find_by_short(device.short_addr, false);
    endpoint_state_t *state =
        slot == NULL ? NULL : endpoint_state(slot, header->src_ep, false);
    if (state == NULL) {
        return;
    }

    bool seen = false;
    for (ezb_zcl_read_attr_rsp_variable_t *item = message->in.variables;
         item != NULL; item = item->next) {
        if (item->attr_id != ZCL_ATTR_IAS_ZONE_TYPE) {
            continue;
        }
        seen = true;
        if (item->status == EZB_ZCL_STATUS_SUCCESS &&
            item->attr_type == EZB_ZCL_ATTR_TYPE_UINT16 &&
            item->attr_value != NULL) {
            uint16_t zone_type = 0U;
            memcpy(&zone_type, item->attr_value, sizeof(zone_type));
            apply_ias_zone_type(slot, state, zone_type);
            return;
        }
    }
    if (seen) {
        state->ias_zone_type_read_requested = false;
        gateway_event_warning(&slot->device, "IAS ZoneType read failed");
    }
}

static void handle_ias_zone_status_change(
    ezb_zcl_ias_zone_status_change_notif_message_t *message)
{
    if (message == NULL) {
        return;
    }
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT) {
        return;
    }
    device_slot_t *slot = recover_report_source(header);
    endpoint_state_t *state =
        slot == NULL ? NULL : endpoint_state(slot, header->src_ep, false);
    if (state == NULL) {
        return;
    }
    state->ias_zone_status = message->in.payload.zone_status;
    state->ias_zone_status_valid = true;
    (void)publish_ias_contact_measurement(
        slot, state, message->in.payload.zone_status);
}

'''
replace_once('main/zigbee_gateway.c',
'''static void publish_report(const ezb_zcl_cmd_report_attr_message_t *message)\n{''',
insert + '''static void publish_report(const ezb_zcl_cmd_report_attr_message_t *message)\n{''')

old_loop = '''        gateway_measurement_kind_t kind;\n        gateway_unit_t unit;\n        double value;\n        if (stable_input && gateway_zcl_normalize(\n                header->cluster_id,\n                item->attr_id,\n                item->attr_type,\n                item->attr_value,\n                &kind,\n                &unit,\n                &value)) {\n            gateway_event_t event = gateway_event_make_input(\n                GATEWAY_EVENT_MEASUREMENT, &input);\n            event.endpoint = header->src_ep;\n            event.data.measurement = (gateway_measurement_t){\n                .kind = kind,\n                .unit = unit,\n                .value = value,\n            };\n            gateway_event_publish(&event);\n        } else {'''
new_loop = '''        gateway_measurement_kind_t kind;\n        gateway_unit_t unit;\n        double value;\n        bool handled = false;\n        if (header->cluster_id == ZCL_CLUSTER_IAS_ZONE &&\n            item->attr_id == ZCL_ATTR_IAS_ZONE_STATUS &&\n            item->attr_type == EZB_ZCL_ATTR_TYPE_UINT16 &&\n            item->attr_value != NULL && slot != NULL) {\n            endpoint_state_t *state = endpoint_state(\n                slot, header->src_ep, false);\n            if (state != NULL) {\n                uint16_t zone_status = 0U;\n                memcpy(&zone_status, item->attr_value, sizeof(zone_status));\n                state->ias_zone_status = zone_status;\n                state->ias_zone_status_valid = true;\n                handled = publish_ias_contact_measurement(\n                    slot, state, zone_status);\n            }\n        }\n        if (!handled && stable_input && gateway_zcl_normalize(\n                header->cluster_id,\n                item->attr_id,\n                item->attr_type,\n                item->attr_value,\n                &kind,\n                &unit,\n                &value)) {\n            gateway_event_t event = gateway_event_make_input(\n                GATEWAY_EVENT_MEASUREMENT, &input);\n            event.endpoint = header->src_ep;\n            event.data.measurement = (gateway_measurement_t){\n                .kind = kind,\n                .unit = unit,\n                .value = value,\n            };\n            gateway_event_publish(&event);\n            handled = true;\n        }\n        if (!handled) {'''
replace_once('main/zigbee_gateway.c', old_loop, new_loop)

replace_once('main/zigbee_gateway.c',
'''    } else if (callback_id == EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID) {\n        publish_basic_read(message);\n    } else if (callback_id == EZB_ZCL_CORE_CONFIG_REPORT_RSP_CB_ID) {''',
'''    } else if (callback_id == EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID) {\n        publish_basic_read(message);\n        publish_ias_zone_type_read(message);\n    } else if (callback_id == EZB_ZCL_CORE_IAS_ZONE_STATUS_CHANGE_NOTIF_CB_ID) {\n        handle_ias_zone_status_change(message);\n    } else if (callback_id == EZB_ZCL_CORE_CONFIG_REPORT_RSP_CB_ID) {''')

replace_once('main/zigbee_gateway.c',
'''        if (state->basic_state == BASIC_SCHEDULED &&\n            (uint32_t)(now_ms - state->basic_scheduled_at_ms) >=\n                GATEWAY_REQUEST_STALE_MS) {\n            state->basic_state = BASIC_NOT_SCHEDULED;\n        }\n''',
'''        if (state->basic_state == BASIC_SCHEDULED &&\n            (uint32_t)(now_ms - state->basic_scheduled_at_ms) >=\n                GATEWAY_REQUEST_STALE_MS) {\n            state->basic_state = BASIC_NOT_SCHEDULED;\n        }\n        if (state->ias_zone_type_read_requested &&\n            (uint32_t)(now_ms - state->ias_zone_type_requested_at_ms) >=\n                GATEWAY_REQUEST_STALE_MS) {\n            state->ias_zone_type_read_requested = false;\n        }\n''')

replace_once('main/zigbee_gateway.c',
'''    const gateway_input_capability_profile_t profile =\n        gateway_zigbee_capability_profile_from_clusters(\n            desc->app_cluster_list, desc->app_input_cluster_count);''',
'''    gateway_input_capability_profile_t profile =\n        gateway_zigbee_capability_profile_from_clusters(\n            desc->app_cluster_list, desc->app_input_cluster_count);\n    if (input_state != NULL && input_state->ias_zone_type_known &&\n        input_state->ias_zone_type == ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH) {\n        profile.readable |= GATEWAY_INPUT_CAP_CONTACT_OPEN;\n    }''')
replace_once('main/zigbee_gateway.c',
'''    bool basic = false;\n    for (uint8_t i = 0; i < desc->app_input_cluster_count; ++i) {''',
'''    bool basic = false;\n    bool ias_zone = false;\n    for (uint8_t i = 0; i < desc->app_input_cluster_count; ++i) {''')
replace_once('main/zigbee_gateway.c',
'''        if (cluster == EZB_ZCL_CLUSTER_ID_BASIC) {\n            basic = true;\n        }\n        if (gateway_reporting_policy_requires_binding(cluster)) {''',
'''        if (cluster == EZB_ZCL_CLUSTER_ID_BASIC) {\n            basic = true;\n        }\n        if (cluster == ZCL_CLUSTER_IAS_ZONE) {\n            ias_zone = true;\n        }\n        if (gateway_reporting_policy_requires_binding(cluster)) {''')
replace_once('main/zigbee_gateway.c',
'''    if (basic) {\n        schedule_basic(slot, desc->ep_id);\n    }\n    context_release(context);''',
'''    if (basic) {\n        schedule_basic(slot, desc->ep_id);\n    }\n    if (ias_zone) {\n        (void)schedule_ias_zone_type(slot, desc->ep_id);\n    }\n    context_release(context);''')

replace_once('main/zigbee_gateway.c',
'''static bool submit_binding(device_slot_t *slot, const discovery_job_t *job)\n{''',
'''static bool submit_ias_zone_type(device_slot_t *slot, const discovery_job_t *job)\n{\n    uint16_t attrs[] = {ZCL_ATTR_IAS_ZONE_TYPE};\n    const ezb_zcl_read_attr_cmd_t request = {\n        .cmd_ctrl = {\n            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),\n            .dst_ep = job->endpoint,\n            .src_ep = GATEWAY_ENDPOINT,\n            .cluster_id = ZCL_CLUSTER_IAS_ZONE,\n            .manuf_code = EZB_ZCL_STD_MANUF_CODE,\n        },\n        .payload = {.attr_number = 1U, .attr_field = attrs},\n    };\n    return ezb_zcl_read_attr_cmd_req(&request) == EZB_ERR_NONE;\n}\n\nstatic bool submit_binding(device_slot_t *slot, const discovery_job_t *job)\n{''')
replace_once('main/zigbee_gateway.c',
'''            job.kind == DISCOVERY_SIMPLE_DESCRIPTOR ? submit_simple(slot, &job) :\n            job.kind == DISCOVERY_READ_BASIC ? submit_basic(slot, &job) :\n            job.kind == DISCOVERY_BIND_CLUSTER ? submit_binding(slot, &job) :''',
'''            job.kind == DISCOVERY_SIMPLE_DESCRIPTOR ? submit_simple(slot, &job) :\n            job.kind == DISCOVERY_READ_BASIC ? submit_basic(slot, &job) :\n            job.kind == DISCOVERY_READ_IAS_ZONE_TYPE ? submit_ias_zone_type(slot, &job) :\n            job.kind == DISCOVERY_BIND_CLUSTER ? submit_binding(slot, &job) :''')

# Ensure the coordinator endpoint advertises an IAS Zone client so standard notifications have a target cluster.
replace_once('main/zigbee_gateway.c',
'''    ezb_af_device_desc_t device = ezb_af_create_device_desc();\n    ezb_af_ep_desc_t endpoint = ezb_af_create_gateway_endpoint(&endpoint_config);\n    if (device == EZB_INVALID_AF_DEVICE_DESC ||\n        endpoint == EZB_INVALID_AF_EP_DESC ||\n        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||''',
'''    ezb_af_device_desc_t device = ezb_af_create_device_desc();\n    ezb_af_ep_desc_t endpoint = ezb_af_create_gateway_endpoint(&endpoint_config);\n    ezb_zcl_cluster_desc_t ias_zone_client = ezb_zcl_ias_zone_create_cluster_desc(\n        NULL, EZB_ZCL_CLUSTER_CLIENT);\n    if (device == EZB_INVALID_AF_DEVICE_DESC ||\n        endpoint == EZB_INVALID_AF_EP_DESC ||\n        ias_zone_client == EZB_INVALID_ZCL_CLUSTER_DESC ||\n        ezb_af_endpoint_add_cluster_desc(endpoint, ias_zone_client) != EZB_ERR_NONE ||\n        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||''')

# Host tests.
replace_once('tests/host/test_gateway_inputs.c',
'''    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_CO2) ==\n           GATEWAY_INPUT_CAP_CO2);\n    assert(gateway_input_capability_for_measurement((gateway_measurement_kind_t)255) == 0U);''',
'''    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_CO2) ==\n           GATEWAY_INPUT_CAP_CO2);\n    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_CONTACT_OPEN) ==\n           GATEWAY_INPUT_CAP_CONTACT_OPEN);\n    assert(gateway_input_capability_for_measurement((gateway_measurement_kind_t)255) == 0U);''')

replace_once('tests/host/test_gateway_zcl_value.c',
'''static void test_attribute_capabilities(void)\n{''',
'''static void test_ias_contact(void)\n{\n    gateway_measurement_kind_t kind;\n    gateway_unit_t unit;\n    double value;\n    assert(gateway_zcl_normalize_ias_contact(\n        0x0015U, 0x0000U, &kind, &unit, &value));\n    assert(kind == GATEWAY_MEAS_CONTACT_OPEN);\n    assert(unit == GATEWAY_UNIT_BOOLEAN);\n    expect_close(value, 0.0);\n    assert(gateway_zcl_normalize_ias_contact(\n        0x0015U, 0x0001U, &kind, &unit, &value));\n    expect_close(value, 1.0);\n    assert(!gateway_zcl_normalize_ias_contact(\n        0x0028U, 0x0001U, &kind, &unit, &value));\n}\n\nstatic void test_attribute_capabilities(void)\n{''')
replace_once('tests/host/test_gateway_zcl_value.c',
'''    test_level();\n    test_attribute_capabilities();''',
'''    test_level();\n    test_ias_contact();\n    test_attribute_capabilities();''')

replace_once('tests/host/test_gateway_link_protocol.c',
'''static void test_measurement_policy_round_trip(void)\n{''',
'''static void test_contact_measurement_round_trip(void)\n{\n    gateway_link_measurement_t measurement = {0};\n    measurement.input.source = GATEWAY_SOURCE_ZIGBEE;\n    measurement.input.channel = 1U;\n    strcpy(measurement.input.id, "zigbee:00124b00aabbccdd");\n    measurement.uptime_ms = 77U;\n    measurement.measurement.kind = GATEWAY_MEAS_CONTACT_OPEN;\n    measurement.measurement.unit = GATEWAY_UNIT_BOOLEAN;\n    measurement.measurement.value = 1.0;\n    measurement.quality = GATEWAY_LINK_QUALITY_VALID;\n\n    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];\n    uint16_t length = 0U;\n    assert(gateway_link_encode_measurement_payload(\n        &measurement, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);\n    gateway_link_measurement_t decoded = {0};\n    assert(gateway_link_decode_measurement_payload(\n        payload, length, &decoded) == GATEWAY_LINK_OK);\n    assert(decoded.measurement.kind == GATEWAY_MEAS_CONTACT_OPEN);\n    assert(decoded.measurement.unit == GATEWAY_UNIT_BOOLEAN);\n    assert(decoded.measurement.value == 1.0);\n}\n\nstatic void test_measurement_policy_round_trip(void)\n{''')
replace_once('tests/host/test_gateway_link_protocol.c',
'''    test_scd41_measurement_round_trip();\n    test_measurement_policy_round_trip();''',
'''    test_scd41_measurement_round_trip();\n    test_contact_measurement_round_trip();\n    test_measurement_policy_round_trip();''')

replace_once('tests/host/test_gateway_zigbee_input.c',
'''        0x0000U, 0x0001U, 0x0006U, 0x0008U, 0x0402U, 0x0405U, 0x0b04U,\n''',
'''        0x0000U, 0x0001U, 0x0006U, 0x0008U, 0x0402U, 0x0405U, 0x0500U, 0x0b04U,\n''')

replace_once('docs/GATEWAY_LINK_V2.md',
'''Standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable` and accept the normalized `SET_ON_OFF` command. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy or command path yet.''',
'''Standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable` and accept the normalized `SET_ON_OFF` command. IAS Zone endpoints do not gain a generic capability from cluster `0x0500` alone: C6 first reads `ZoneType`, and only `ContactSwitch` (`0x0015`) exposes readable `CONTACT_OPEN`. `ZoneStatus.Alarm1` maps to boolean `CONTACT_OPEN=true`; IAS contact is not advertised as configurable/reportable through Configure Reporting. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy or command path yet.''')
replace_once('docs/GATEWAY_LINK_V2.md',
'''MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload encodings remain otherwise unchanged from v1.''',
'''Measurement wire kind `15` is `CONTACT_OPEN`. IAS contact state can arrive through the standard IAS `ZoneStatusChangeNotification` callback or a `ZoneStatus` attribute report; both paths are type-gated by the cached `ZoneType` before normalization. MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload encodings remain otherwise unchanged from v1.''')
PY

python3 - <<'PY'
from pathlib import Path
bad=[]
for root in ['main','tests','docs']:
    for p in Path(root).rglob('*'):
        if p.is_file() and b'\x00' in p.read_bytes():
            bad.append(str(p))
if bad:
    raise SystemExit('embedded NUL: ' + ', '.join(bad))
print('source NUL scan passed')
PY

git diff --check

CC=${CC:-cc}
CFLAGS='-std=c11 -Wall -Wextra -Werror -Imain'
$CC $CFLAGS tests/host/test_gateway_inputs.c main/gateway_inputs.c -o /tmp/test_gateway_inputs
/tmp/test_gateway_inputs
$CC $CFLAGS -DGATEWAY_ZCL_HOST_TEST tests/host/test_gateway_zcl_value.c main/gateway_zcl_value.c main/gateway_inputs.c -lm -o /tmp/test_gateway_zcl_value
/tmp/test_gateway_zcl_value
$CC $CFLAGS tests/host/test_gateway_link_protocol.c main/gateway_link_protocol.c main/gateway_inputs.c -lm -o /tmp/test_gateway_link_protocol
/tmp/test_gateway_link_protocol
$CC $CFLAGS -DGATEWAY_ZCL_HOST_TEST tests/host/test_gateway_zigbee_input.c main/gateway_zigbee_input.c main/gateway_zcl_value.c main/gateway_reporting_policy.c main/gateway_inputs.c -lm -o /tmp/test_gateway_zigbee_input
/tmp/test_gateway_zigbee_input

if ! command -v idf.py >/dev/null 2>&1; then
    if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        . "$HOME/esp/esp-idf/export.sh" >/dev/null
    elif [ -f "/opt/esp/idf/export.sh" ]; then
        . "/opt/esp/idf/export.sh" >/dev/null
    fi
fi
command -v idf.py >/dev/null 2>&1
idf.py build
idf.py size

python3 tests/host/test_no_embedded_nul.py

git diff --check
git status --short

git add \
  main/gateway_inputs.h main/gateway_inputs.c \
  main/gateway_link_protocol.c \
  main/gateway_zcl_value.h main/gateway_zcl_value.c \
  main/gateway_device_state.h main/zigbee_gateway.c \
  tests/host/test_gateway_inputs.c \
  tests/host/test_gateway_zcl_value.c \
  tests/host/test_gateway_link_protocol.c \
  tests/host/test_gateway_zigbee_input.c \
  docs/GATEWAY_LINK_V2.md

git diff --cached --check
git commit -m "Add ZoneType-aware IAS contact inputs"
git push origin HEAD:"$BRANCH"
echo "IAS_CONTACT_PRODUCTION_HEAD=$(git rev-parse HEAD)"
