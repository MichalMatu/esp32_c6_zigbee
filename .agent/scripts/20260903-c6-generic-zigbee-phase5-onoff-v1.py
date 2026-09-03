from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    if text.count(old) != 1:
        raise SystemExit(f"expected one match in {path}: {old[:120]!r}, got {text.count(old)}")
    write(path, text.replace(old, new, 1))


def insert_before(path, marker, text_to_insert):
    text = read(path)
    if text.count(marker) != 1:
        raise SystemExit(f"expected one marker in {path}: {marker[:120]!r}, got {text.count(marker)}")
    write(path, text.replace(marker, text_to_insert + marker, 1))


def insert_after(path, marker, text_to_insert):
    text = read(path)
    if text.count(marker) != 1:
        raise SystemExit(f"expected one marker in {path}: {marker[:120]!r}, got {text.count(marker)}")
    write(path, text.replace(marker, marker + text_to_insert, 1))


# Source-neutral command kind.
replace_once(
    "main/gateway_inputs.h",
    "typedef enum {\n    GATEWAY_UNIT_NONE,",
    "typedef enum {\n    GATEWAY_COMMAND_SET_ON_OFF = 0,\n} gateway_command_kind_t;\n\ntypedef enum {\n    GATEWAY_UNIT_NONE,",
)

# Pure command policy: normalized command -> standard Zigbee semantic plan.
write("main/gateway_command_policy.h", r'''#pragma once

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
''')

write("main/gateway_command_policy.c", r'''#include "gateway_command_policy.h"

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
''')

write("tests/host/test_gateway_command_policy.c", r'''#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "gateway_command_policy.h"

static void test_on_off_plan(void)
{
    gateway_command_plan_t plan;
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 1.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(plan.cluster_id == 0x0006U);
    assert(plan.capability == GATEWAY_INPUT_CAP_ON_OFF);
    assert(plan.target_on);

    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 0.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(!plan.target_on);
}

static void test_invalid_on_off_values(void)
{
    gateway_command_plan_t plan;
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 0.5, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, NAN, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 1.0, 100U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        (gateway_command_kind_t)99, 1.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_UNSUPPORTED);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 1.0, 0U, NULL) == GATEWAY_COMMAND_PLAN_INVALID);
}

int main(void)
{
    test_on_off_plan();
    test_invalid_on_off_values();
    puts("gateway_command_policy host tests passed");
    return 0;
}
''')

replace_once(
    "main/CMakeLists.txt",
    '        "gateway_console.c"\n',
    '        "gateway_console.c"\n        "gateway_command_policy.c"\n',
)

# GatewayLink v2 command request/result contract.
replace_once(
    "main/gateway_link_protocol.h",
    "#define GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE (1UL << 3)\n",
    "#define GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE (1UL << 3)\n#define GATEWAY_LINK_FEATURE_COMMANDS           (1UL << 4)\n",
)
replace_once(
    "main/gateway_link_protocol.h",
    "    GATEWAY_LINK_MSG_PERMIT_JOIN = 0x22,\n",
    "    GATEWAY_LINK_MSG_PERMIT_JOIN = 0x22,\n    GATEWAY_LINK_MSG_COMMAND_REQUEST = 0x30,\n    GATEWAY_LINK_MSG_COMMAND_RESULT = 0x31,\n",
)
insert_after(
    "main/gateway_link_protocol.h",
    "typedef enum {\n    GATEWAY_LINK_CONFIG_APPLIED = 0,\n    GATEWAY_LINK_CONFIG_CLAMPED = 1,\n    GATEWAY_LINK_CONFIG_UNSUPPORTED = 2,\n    GATEWAY_LINK_CONFIG_ERROR = 3,\n} gateway_link_config_status_t;\n",
    r'''

typedef enum {
    GATEWAY_LINK_COMMAND_TRANSMITTED = 0,
    GATEWAY_LINK_COMMAND_UNSUPPORTED = 1,
    GATEWAY_LINK_COMMAND_INVALID = 2,
    GATEWAY_LINK_COMMAND_ERROR = 3,
} gateway_link_command_status_t;
''',
)
insert_after(
    "main/gateway_link_protocol.h",
    "typedef struct {\n    uint32_t request_id;\n    uint8_t duration_seconds;\n} gateway_link_permit_join_t;\n",
    r'''

typedef struct {
    uint32_t request_id;
    gateway_input_id_t input;
    gateway_command_kind_t kind;
    double value;
    uint32_t transition_ms;
} gateway_link_command_request_t;

typedef struct {
    uint32_t request_id;
    gateway_link_command_status_t status;
} gateway_link_command_result_t;
''',
)
insert_before(
    "main/gateway_link_protocol.h",
    "gateway_link_result_t gateway_link_encode_u32_payload(\n",
    r'''gateway_link_result_t gateway_link_encode_command_request_payload(
    const gateway_link_command_request_t *command,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_command_request_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_command_request_t *command);

gateway_link_result_t gateway_link_encode_command_result_payload(
    const gateway_link_command_result_t *result,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_command_result_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_command_result_t *result);

''',
)

insert_before(
    "main/gateway_link_protocol.c",
    "static gateway_link_result_t unit_to_wire(gateway_unit_t unit, uint8_t *wire)\n",
    r'''static gateway_link_result_t command_kind_to_wire(
    gateway_command_kind_t kind, uint8_t *wire)
{
    if (wire == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (kind != GATEWAY_COMMAND_SET_ON_OFF) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    *wire = 0U;
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t command_kind_from_wire(
    uint8_t wire, gateway_command_kind_t *kind)
{
    if (kind == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (wire != 0U) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    *kind = GATEWAY_COMMAND_SET_ON_OFF;
    return GATEWAY_LINK_OK;
}

''',
)
insert_before(
    "main/gateway_link_protocol.c",
    "gateway_link_result_t gateway_link_encode_config_result_payload(\n",
    r'''gateway_link_result_t gateway_link_encode_command_request_payload(
    const gateway_link_command_request_t *command,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (command == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 4U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    write_u32_le(payload, command->request_id);
    size_t used = 4U;
    size_t ref_used = 0U;
    gateway_link_result_t result = encode_input_ref(
        &command->input, &payload[used], capacity - used, &ref_used);
    if (result != GATEWAY_LINK_OK) return result;
    used += ref_used;
    if (used + 13U > capacity) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    uint8_t kind = 0U;
    result = command_kind_to_wire(command->kind, &kind);
    if (result != GATEWAY_LINK_OK) return result;
    payload[used++] = kind;
    write_double_le(&payload[used], command->value);
    used += 8U;
    write_u32_le(&payload[used], command->transition_ms);
    used += 4U;
    *length = (uint16_t)used;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_command_request_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_command_request_t *command)
{
    if (payload == NULL || command == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length < 4U) return GATEWAY_LINK_MALFORMED;
    memset(command, 0, sizeof(*command));
    command->request_id = read_u32_le(payload);
    size_t used = 4U;
    size_t ref_used = 0U;
    gateway_link_result_t result = decode_input_ref(
        &payload[used], length - used, &command->input, &ref_used);
    if (result != GATEWAY_LINK_OK) return result;
    used += ref_used;
    if (used + 13U != length) return GATEWAY_LINK_MALFORMED;
    result = command_kind_from_wire(payload[used++], &command->kind);
    if (result != GATEWAY_LINK_OK) return result;
    command->value = read_double_le(&payload[used]);
    used += 8U;
    command->transition_ms = read_u32_le(&payload[used]);
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_command_result_payload(
    const gateway_link_command_result_t *result,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (result == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 5U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    if (result->status > GATEWAY_LINK_COMMAND_ERROR) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    write_u32_le(payload, result->request_id);
    payload[4] = (uint8_t)result->status;
    *length = 5U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_command_result_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_command_result_t *result)
{
    if (payload == NULL || result == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length != 5U) return GATEWAY_LINK_MALFORMED;
    if (payload[4] > GATEWAY_LINK_COMMAND_ERROR) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    result->request_id = read_u32_le(payload);
    result->status = (gateway_link_command_status_t)payload[4];
    return GATEWAY_LINK_OK;
}

''',
)

# GatewayLink control parsing/builders and feature advertisement.
replace_once(
    "main/gateway_link_control.h",
    "    GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY,\n    GATEWAY_LINK_CONTROL_INVALID,",
    "    GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY,\n    GATEWAY_LINK_CONTROL_COMMAND,\n    GATEWAY_LINK_CONTROL_INVALID,",
)
replace_once(
    "main/gateway_link_control.h",
    "    gateway_link_measurement_policy_t measurement_policy;\n",
    "    gateway_link_measurement_policy_t measurement_policy;\n    gateway_link_command_request_t command;\n",
)
insert_before(
    "main/gateway_link_control.h",
    "bool gateway_link_make_snapshot_marker_message(\n",
    r'''bool gateway_link_make_command_result_message(
    uint32_t request_id,
    gateway_link_command_status_t status,
    gateway_link_message_t *message);
''',
)
replace_once(
    "main/gateway_link_control.c",
    "            GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,\n",
    "            GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE |\n            GATEWAY_LINK_FEATURE_COMMANDS,\n",
)
insert_before(
    "main/gateway_link_control.c",
    "    default:\n        action.kind = GATEWAY_LINK_CONTROL_IGNORE;",
    r'''    case GATEWAY_LINK_MSG_COMMAND_REQUEST:
    {
        gateway_link_command_request_t command = {0};
        if (gateway_link_decode_command_request_payload(
                frame->payload, frame->payload_length, &command) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_COMMAND;
        action.request_id = command.request_id;
        action.command = command;
        return action;
    }

''',
)
insert_before(
    "main/gateway_link_control.c",
    "bool gateway_link_make_snapshot_marker_message(\n",
    r'''bool gateway_link_make_command_result_message(
    uint32_t request_id,
    gateway_link_command_status_t status,
    gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_COMMAND_RESULT;
    const gateway_link_command_result_t result = {
        .request_id = request_id,
        .status = status,
    };
    return gateway_link_encode_command_result_payload(
        &result, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

''',
)

# Normalized command completion event.
replace_once(
    "main/gateway_events.h",
    "    GATEWAY_EVENT_REPORTING_CONFIG,\n    GATEWAY_EVENT_INPUT_AVAILABLE,",
    "    GATEWAY_EVENT_REPORTING_CONFIG,\n    GATEWAY_EVENT_COMMAND_RESULT,\n    GATEWAY_EVENT_INPUT_AVAILABLE,",
)
insert_after(
    "main/gateway_events.h",
    "typedef enum {\n    GATEWAY_EVENT_CONFIG_NONE = 0,\n    GATEWAY_EVENT_CONFIG_APPLIED,\n    GATEWAY_EVENT_CONFIG_CLAMPED,\n    GATEWAY_EVENT_CONFIG_UNSUPPORTED,\n    GATEWAY_EVENT_CONFIG_ERROR,\n} gateway_event_config_result_t;\n",
    r'''

typedef enum {
    GATEWAY_EVENT_COMMAND_TRANSMITTED = 0,
    GATEWAY_EVENT_COMMAND_UNSUPPORTED,
    GATEWAY_EVENT_COMMAND_INVALID,
    GATEWAY_EVENT_COMMAND_ERROR,
} gateway_event_command_result_t;
''',
)
replace_once(
    "main/gateway_events.h",
    "        struct {\n            uint16_t cluster_id;\n            uint8_t status;\n        } binding;\n",
    "        struct {\n            uint16_t cluster_id;\n            uint8_t status;\n        } binding;\n        struct {\n            uint32_t request_id;\n            gateway_event_command_result_t result;\n            uint8_t status;\n            uint8_t tsn;\n        } command;\n",
)

insert_before(
    "main/gateway_link_event_adapter.c",
    "    if (event->kind == GATEWAY_EVENT_MEASUREMENT) {\n",
    r'''    if (event->kind == GATEWAY_EVENT_COMMAND_RESULT &&
        event->data.command.request_id != 0U) {
        gateway_link_command_status_t status = GATEWAY_LINK_COMMAND_ERROR;
        switch (event->data.command.result) {
        case GATEWAY_EVENT_COMMAND_TRANSMITTED:
            status = GATEWAY_LINK_COMMAND_TRANSMITTED;
            break;
        case GATEWAY_EVENT_COMMAND_UNSUPPORTED:
            status = GATEWAY_LINK_COMMAND_UNSUPPORTED;
            break;
        case GATEWAY_EVENT_COMMAND_INVALID:
            status = GATEWAY_LINK_COMMAND_INVALID;
            break;
        case GATEWAY_EVENT_COMMAND_ERROR:
        default:
            status = GATEWAY_LINK_COMMAND_ERROR;
            break;
        }
        message->type = GATEWAY_LINK_MSG_COMMAND_RESULT;
        const gateway_link_command_result_t result = {
            .request_id = event->data.command.request_id,
            .status = status,
        };
        return gateway_link_encode_command_result_payload(
            &result, message->payload, sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

''',
)

insert_before(
    "main/gateway_transport.c",
    "    case GATEWAY_EVENT_INPUT_AVAILABLE:\n",
    r'''    case GATEWAY_EVENT_COMMAND_RESULT:
        ESP_LOGI(TAG,
                 "command result request=%" PRIu32 " result=%u af_status=0x%02x tsn=%u",
                 event->data.command.request_id, (unsigned)event->data.command.result,
                 event->data.command.status, event->data.command.tsn);
        break;
''',
)

# Zigbee command submit API.
insert_after(
    "main/zigbee_gateway.h",
    "} zigbee_gateway_policy_submit_result_t;\n",
    r'''

typedef enum {
    ZIGBEE_GATEWAY_COMMAND_QUEUED = 0,
    ZIGBEE_GATEWAY_COMMAND_UNSUPPORTED,
    ZIGBEE_GATEWAY_COMMAND_INVALID,
    ZIGBEE_GATEWAY_COMMAND_ERROR,
} zigbee_gateway_command_submit_result_t;
''',
)
insert_before(
    "main/zigbee_gateway.h",
    "zigbee_gateway_policy_submit_result_t zigbee_gateway_set_measurement_policy(\n",
    r'''zigbee_gateway_command_submit_result_t zigbee_gateway_submit_command(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_command_kind_t kind,
    double value,
    uint32_t transition_ms);
''',
)

# Zigbee runtime: new ezbee On/Off API, serialized external command and bounded confirmation contexts.
replace_once(
    "main/zigbee_gateway.c",
    "#include <ezbee/zcl/cluster/poll_control.h>\n",
    "#include <ezbee/zcl/cluster/on_off.h>\n#include <ezbee/zcl/cluster/poll_control.h>\n",
)
replace_once(
    "main/zigbee_gateway.c",
    '#include "gateway_device_state.h"\n',
    '#include "gateway_command_policy.h"\n#include "gateway_device_state.h"\n',
)
replace_once(
    "main/zigbee_gateway.c",
    "#define GATEWAY_MAX_ASYNC_CONTEXTS 32U\n",
    "#define GATEWAY_MAX_ASYNC_CONTEXTS 32U\n#define GATEWAY_MAX_COMMAND_CONTEXTS 16U\n",
)
replace_once(
    "main/zigbee_gateway.c",
    "    DISCOVERY_EXTERNAL_REPORTING,\n} discovery_kind_t;",
    "    DISCOVERY_EXTERNAL_REPORTING,\n    DISCOVERY_EXTERNAL_COMMAND,\n} discovery_kind_t;",
)
replace_once(
    "main/zigbee_gateway.c",
    "    gateway_measurement_kind_t external_kind;\n} discovery_job_t;",
    "    gateway_measurement_kind_t external_kind;\n    gateway_command_plan_t command_plan;\n    uint32_t command_request_id;\n} discovery_job_t;",
)
insert_after(
    "main/zigbee_gateway.c",
    "typedef struct {\n    bool in_use;\n    device_ref_t device;\n    ezb_shortaddr_t route_short_addr;\n    uint8_t endpoint;\n    uint16_t cluster_id;\n    uint8_t retry_count;\n} async_context_t;\n",
    r'''

typedef struct {
    bool in_use;
    device_ref_t device;
    ezb_shortaddr_t route_short_addr;
    uint8_t endpoint;
    uint32_t request_id;
    gateway_input_id_t input;
} command_context_t;
''',
)
replace_once(
    "main/zigbee_gateway.c",
    "static async_context_t s_async_contexts[GATEWAY_MAX_ASYNC_CONTEXTS];\n",
    "static async_context_t s_async_contexts[GATEWAY_MAX_ASYNC_CONTEXTS];\nstatic command_context_t s_command_contexts[GATEWAY_MAX_COMMAND_CONTEXTS];\n",
)

# Command helper block before discovery task.
insert_before(
    "main/zigbee_gateway.c",
    "static void discovery_task(void *arg)\n",
    r'''static void publish_command_result(
    const gateway_input_id_t *input,
    uint32_t request_id,
    gateway_event_command_result_t result,
    uint8_t status,
    uint8_t tsn)
{
    if (input == NULL || request_id == 0U) {
        return;
    }
    gateway_event_t event = gateway_event_make_input(GATEWAY_EVENT_COMMAND_RESULT, input);
    event.data.command.request_id = request_id;
    event.data.command.result = result;
    event.data.command.status = status;
    event.data.command.tsn = tsn;
    gateway_event_publish(&event);
}

static command_context_t *command_context_alloc(
    device_slot_t *slot,
    const gateway_input_id_t *input,
    uint8_t endpoint,
    uint32_t request_id)
{
    if (slot == NULL || input == NULL || request_id == 0U) {
        return NULL;
    }
    for (size_t i = 0U; i < GATEWAY_MAX_COMMAND_CONTEXTS; ++i) {
        if (!s_command_contexts[i].in_use) {
            s_command_contexts[i] = (command_context_t){
                .in_use = true,
                .device = gateway_device_ref_for(slot),
                .route_short_addr = slot->device.short_addr,
                .endpoint = endpoint,
                .request_id = request_id,
                .input = *input,
            };
            ++slot->pending_requests;
            return &s_command_contexts[i];
        }
    }
    return NULL;
}

static void command_context_release(command_context_t *context)
{
    if (context == NULL || !context->in_use) {
        return;
    }
    device_slot_t *slot = gateway_device_from_ref(context->device, true);
    if (slot != NULL && slot->pending_requests != 0U) {
        --slot->pending_requests;
        gateway_device_maybe_reclaim(slot);
    }
    context->in_use = false;
}

static void command_confirm_callback(ezb_af_user_cnf_t *cnf, void *user_ctx)
{
    command_context_t *context = user_ctx;
    if (context == NULL || !context->in_use) {
        return;
    }
    const uint8_t status = cnf == NULL ? 0xffU : cnf->status;
    const uint8_t tsn = cnf == NULL ? 0xffU : cnf->tsn;
    device_slot_t *slot = gateway_device_from_ref(context->device, true);
    const bool route_current = slot != NULL && slot->state == SLOT_ACTIVE &&
        slot->device.short_addr == context->route_short_addr;
    publish_command_result(
        &context->input,
        context->request_id,
        cnf != NULL && status == 0U && route_current ?
            GATEWAY_EVENT_COMMAND_TRANSMITTED : GATEWAY_EVENT_COMMAND_ERROR,
        status,
        tsn);
    command_context_release(context);
}

static void handle_external_command(const discovery_job_t *job)
{
    if (job == NULL || job->command_request_id == 0U) {
        return;
    }
    uint8_t ieee[8];
    uint8_t endpoint = 0U;
    if (!gateway_zigbee_parse_input_identity(&job->external_input, ieee, &endpoint)) {
        publish_command_result(
            &job->external_input, job->command_request_id,
            GATEWAY_EVENT_COMMAND_INVALID, 0xffU, 0xffU);
        return;
    }
    device_slot_t *slot = gateway_device_find_by_ieee(ieee, false);
    endpoint_state_t *state = slot == NULL ? NULL : endpoint_state(slot, endpoint, false);
    if (slot == NULL || state == NULL || slot->device.short_addr == GATEWAY_INVALID_SHORT_ADDR) {
        publish_command_result(
            &job->external_input, job->command_request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        return;
    }
    if ((state->input_profile.commandable & job->command_plan.capability) == 0U) {
        publish_command_result(
            &job->external_input, job->command_request_id,
            GATEWAY_EVENT_COMMAND_UNSUPPORTED, 0xffU, 0xffU);
        return;
    }
    command_context_t *context = command_context_alloc(
        slot, &job->external_input, endpoint, job->command_request_id);
    if (context == NULL) {
        publish_command_result(
            &job->external_input, job->command_request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        return;
    }
    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS))) {
        publish_command_result(
            &job->external_input, job->command_request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        command_context_release(context);
        return;
    }
    const ezb_zcl_on_off_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = endpoint,
            .src_ep = GATEWAY_ENDPOINT,
            .dis_default_rsp = false,
            .cnf_ctx = {
                .cb = command_confirm_callback,
                .user_ctx = context,
            },
        },
    };
    const ezb_err_t send_result = job->command_plan.target_on ?
        ezb_zcl_on_off_on_cmd_req(&request) : ezb_zcl_on_off_off_cmd_req(&request);
    esp_zigbee_lock_release();
    if (send_result != EZB_ERR_NONE && context->in_use) {
        publish_command_result(
            &job->external_input, job->command_request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        command_context_release(context);
    }
}

''',
)
replace_once(
    "main/zigbee_gateway.c",
    "        if (job.kind == DISCOVERY_EXTERNAL_REPORTING) {\n            handle_external_reporting(&job);\n            continue;\n        }\n",
    "        if (job.kind == DISCOVERY_EXTERNAL_REPORTING) {\n            handle_external_reporting(&job);\n            continue;\n        }\n        if (job.kind == DISCOVERY_EXTERNAL_COMMAND) {\n            handle_external_command(&job);\n            continue;\n        }\n",
)

insert_before(
    "main/zigbee_gateway.c",
    "zigbee_gateway_policy_submit_result_t zigbee_gateway_set_measurement_policy(\n",
    r'''zigbee_gateway_command_submit_result_t zigbee_gateway_submit_command(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_command_kind_t kind,
    double value,
    uint32_t transition_ms)
{
    if (request_id == 0U || input == NULL) {
        return ZIGBEE_GATEWAY_COMMAND_INVALID;
    }
    if (input->source != GATEWAY_SOURCE_ZIGBEE) {
        return ZIGBEE_GATEWAY_COMMAND_UNSUPPORTED;
    }
    uint8_t ieee[8];
    uint8_t endpoint = 0U;
    if (!gateway_zigbee_parse_input_identity(input, ieee, &endpoint)) {
        return ZIGBEE_GATEWAY_COMMAND_INVALID;
    }
    gateway_command_plan_t plan;
    const gateway_command_plan_result_t plan_result = gateway_command_policy_plan(
        kind, value, transition_ms, &plan);
    if (plan_result == GATEWAY_COMMAND_PLAN_UNSUPPORTED) {
        return ZIGBEE_GATEWAY_COMMAND_UNSUPPORTED;
    }
    if (plan_result != GATEWAY_COMMAND_PLAN_OK) {
        return ZIGBEE_GATEWAY_COMMAND_INVALID;
    }
    if (!stack_is_ready() || s_discovery_queue == NULL) {
        return ZIGBEE_GATEWAY_COMMAND_ERROR;
    }
    const discovery_job_t job = {
        .kind = DISCOVERY_EXTERNAL_COMMAND,
        .external_input = *input,
        .command_plan = plan,
        .command_request_id = request_id,
    };
    return xQueueSend(s_discovery_queue, &job, 0U) == pdPASS ?
        ZIGBEE_GATEWAY_COMMAND_QUEUED : ZIGBEE_GATEWAY_COMMAND_ERROR;
}

''',
)

# GatewayLink RX -> Zigbee command API.
insert_before(
    "main/gateway_link.c",
    "    case GATEWAY_LINK_CONTROL_INVALID:\n",
    r'''    case GATEWAY_LINK_CONTROL_COMMAND:
    {
        const gateway_link_command_request_t *command = &action.command;
        const zigbee_gateway_command_submit_result_t result =
            zigbee_gateway_submit_command(
                command->request_id, &command->input, command->kind,
                command->value, command->transition_ms);
        if (result == ZIGBEE_GATEWAY_COMMAND_QUEUED) {
            break;
        }
        const gateway_link_command_status_t status =
            result == ZIGBEE_GATEWAY_COMMAND_UNSUPPORTED ? GATEWAY_LINK_COMMAND_UNSUPPORTED :
            result == ZIGBEE_GATEWAY_COMMAND_INVALID ? GATEWAY_LINK_COMMAND_INVALID :
            GATEWAY_LINK_COMMAND_ERROR;
        if (gateway_link_make_command_result_message(
                command->request_id, status, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    }
''',
)

# Protocol host tests.
insert_before(
    "tests/host/test_gateway_link_protocol.c",
    "static void test_small_buffer_and_unknown_source_fail(void)\n",
    r'''static void test_command_round_trip(void)
{
    gateway_link_command_request_t command = {0};
    command.request_id = 0x55667788U;
    command.input.source = GATEWAY_SOURCE_ZIGBEE;
    command.input.channel = 2U;
    strcpy(command.input.id, "zigbee:00124b00aabbccdd");
    command.kind = GATEWAY_COMMAND_SET_ON_OFF;
    command.value = 1.0;
    command.transition_ms = 0U;

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_command_request_payload(
        &command, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);
    gateway_link_command_request_t decoded = {0};
    assert(gateway_link_decode_command_request_payload(
        payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == command.request_id);
    assert(decoded.input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(decoded.input.channel == 2U);
    assert(strcmp(decoded.input.id, command.input.id) == 0);
    assert(decoded.kind == GATEWAY_COMMAND_SET_ON_OFF);
    assert(decoded.value == 1.0);
    assert(decoded.transition_ms == 0U);

    const gateway_link_command_result_t result = {
        .request_id = command.request_id,
        .status = GATEWAY_LINK_COMMAND_TRANSMITTED,
    };
    assert(gateway_link_encode_command_result_payload(
        &result, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);
    gateway_link_command_result_t decoded_result = {0};
    assert(gateway_link_decode_command_result_payload(
        payload, length, &decoded_result) == GATEWAY_LINK_OK);
    assert(decoded_result.request_id == command.request_id);
    assert(decoded_result.status == GATEWAY_LINK_COMMAND_TRANSMITTED);

    decoded_result.status = (gateway_link_command_status_t)99;
    assert(gateway_link_encode_command_result_payload(
        &decoded_result, payload, sizeof(payload), &length) == GATEWAY_LINK_UNSUPPORTED_VALUE);
}

''',
)
replace_once(
    "tests/host/test_gateway_link_protocol.c",
    "    test_measurement_policy_round_trip();\n    test_small_buffer_and_unknown_source_fail();",
    "    test_measurement_policy_round_trip();\n    test_command_round_trip();\n    test_small_buffer_and_unknown_source_fail();",
)
replace_once(
    "tests/host/test_gateway_link_protocol.c",
    "            GATEWAY_LINK_FEATURE_PERMIT_JOIN |\n            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,",
    "            GATEWAY_LINK_FEATURE_PERMIT_JOIN |\n            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE |\n            GATEWAY_LINK_FEATURE_COMMANDS,",
)

# Control host tests.
replace_once(
    "tests/host/test_gateway_link_control.c",
    "        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE));",
    "        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE |\n        GATEWAY_LINK_FEATURE_COMMANDS));",
)
# replace second identical expected feature occurrence after first replacement
replace_once(
    "tests/host/test_gateway_link_control.c",
    "        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE));",
    "        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE |\n        GATEWAY_LINK_FEATURE_COMMANDS));",
)
insert_before(
    "tests/host/test_gateway_link_control.c",
    "static void test_malformed_control_is_invalid(void)\n",
    r'''static void test_command_request_and_result(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_COMMAND_REQUEST};
    gateway_link_command_request_t command = {0};
    command.request_id = 88U;
    command.input.source = GATEWAY_SOURCE_ZIGBEE;
    command.input.channel = 1U;
    strcpy(command.input.id, "zigbee:00124b00aabbccdd");
    command.kind = GATEWAY_COMMAND_SET_ON_OFF;
    command.value = 0.0;
    assert(gateway_link_encode_command_request_payload(
        &command, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_COMMAND);
    assert(action.request_id == 88U);
    assert(action.command.kind == GATEWAY_COMMAND_SET_ON_OFF);
    assert(action.command.value == 0.0);

    gateway_link_message_t response;
    assert(gateway_link_make_command_result_message(
        88U, GATEWAY_LINK_COMMAND_TRANSMITTED, &response));
    assert(response.type == GATEWAY_LINK_MSG_COMMAND_RESULT);
    gateway_link_command_result_t decoded = {0};
    assert(gateway_link_decode_command_result_payload(
        response.payload, response.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 88U);
    assert(decoded.status == GATEWAY_LINK_COMMAND_TRANSMITTED);
}

''',
)
replace_once(
    "tests/host/test_gateway_link_control.c",
    "    test_measurement_policy_request();\n    test_malformed_control_is_invalid();",
    "    test_measurement_policy_request();\n    test_command_request_and_result();\n    test_malformed_control_is_invalid();",
)

# Event adapter host test.
insert_before(
    "tests/host/test_gateway_link_event_adapter.c",
    "static void test_protocol_specific_event_is_not_forwarded(void)\n",
    r'''static void test_command_result_event(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_COMMAND_RESULT;
    event.data.command.request_id = 123U;
    event.data.command.result = GATEWAY_EVENT_COMMAND_TRANSMITTED;
    event.data.command.status = 0U;
    event.data.command.tsn = 9U;
    gateway_link_message_t message;
    assert(gateway_link_message_from_event(&event, &message));
    assert(message.type == GATEWAY_LINK_MSG_COMMAND_RESULT);
    gateway_link_command_result_t decoded = {0};
    assert(gateway_link_decode_command_result_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 123U);
    assert(decoded.status == GATEWAY_LINK_COMMAND_TRANSMITTED);

    event.data.command.result = GATEWAY_EVENT_COMMAND_ERROR;
    assert(gateway_link_message_from_event(&event, &message));
    assert(gateway_link_decode_command_result_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.status == GATEWAY_LINK_COMMAND_ERROR);

    event.data.command.request_id = 0U;
    assert(!gateway_link_message_from_event(&event, &message));
}

''',
)
replace_once(
    "tests/host/test_gateway_link_event_adapter.c",
    "    test_reporting_config_result();\n    test_protocol_specific_event_is_not_forwarded();",
    "    test_reporting_config_result();\n    test_command_result_event();\n    test_protocol_specific_event_is_not_forwarded();",
)

# Virtual-S3 E2E command exchange.
insert_before(
    "tests/host/test_gateway_link_e2e.c",
    "int main(void)\n",
    r'''static void test_command_request_result_round_trip(void)
{
    gateway_link_command_request_t request = {0};
    request.request_id = 7001U;
    request.input = gateway_input_make(
        GATEWAY_SOURCE_ZIGBEE, "zigbee:00124b00aabbccdd", 1U);
    request.kind = GATEWAY_COMMAND_SET_ON_OFF;
    request.value = 1.0;
    request.transition_ms = 0U;

    gateway_link_message_t outgoing = {.type = GATEWAY_LINK_MSG_COMMAND_REQUEST};
    assert(gateway_link_encode_command_request_payload(
        &request, outgoing.payload, sizeof(outgoing.payload),
        &outgoing.payload_length) == GATEWAY_LINK_OK);
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t encoded_length = encode_message(&outgoing, 900U, encoded);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    size_t frames = 0U;
    size_t drops = 0U;
    assert(feed_all(&decoder, encoded, encoded_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frames == 1U && drops == 0U);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_COMMAND);
    assert(action.command.request_id == 7001U);
    assert(action.command.value == 1.0);

    gateway_link_message_t response;
    assert(gateway_link_make_command_result_message(
        action.request_id, GATEWAY_LINK_COMMAND_TRANSMITTED, &response));
    const size_t response_length = encode_message(&response, 901U, encoded);
    gateway_link_stream_init(&decoder);
    frames = drops = 0U;
    assert(feed_all(&decoder, encoded, response_length,
                    &frame, &result, &frames, &drops) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.type == GATEWAY_LINK_MSG_COMMAND_RESULT);
    gateway_link_command_result_t decoded = {0};
    assert(gateway_link_decode_command_result_payload(
        frame.payload, frame.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 7001U);
    assert(decoded.status == GATEWAY_LINK_COMMAND_TRANSMITTED);
}

''',
)
replace_once(
    "tests/host/test_gateway_link_e2e.c",
    "    test_snapshot_replay_and_policy_control();\n",
    "    test_snapshot_replay_and_policy_control();\n    test_command_request_result_round_trip();\n",
)
replace_once(
    "tests/host/test_gateway_link_e2e.c",
    "    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) != 0U);\n",
    "    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) != 0U);\n    assert((c6_hello.features & GATEWAY_LINK_FEATURE_COMMANDS) != 0U);\n",
)

# CI host test for pure command policy.
insert_before(
    ".github/workflows/quality.yml",
    "  host-link-protocol:\n",
    r'''  host-command-policy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build command policy host tests
        run: |
          cc -std=c11 -Wall -Wextra -Werror -pedantic \
            -Imain \
            tests/host/test_gateway_command_policy.c \
            main/gateway_command_policy.c \
            -lm \
            -o /tmp/test_gateway_command_policy
      - name: Run command policy host tests
        run: /tmp/test_gateway_command_policy

''',
)

# Documentation: current v2 wire contract and continuation progress.
replace_once(
    "docs/GATEWAY_LINK_V2.md",
    "At this phase, standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable`; the actual command request/result path is added in the following implementation phase. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy yet.",
    "Standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable` and accept the normalized `SET_ON_OFF` command. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy or command path yet.",
)
replace_once(
    "docs/GATEWAY_LINK_V2.md",
    "Message numbers remain: HELLO/ACK `0x01/0x02`, PING/PONG `0x03/0x04`, snapshot `0x05..0x07`, INPUT_DESCRIPTOR `0x10`, MEASUREMENT `0x11`, SET_MEASUREMENT_POLICY `0x20`, CONFIG_RESULT `0x21`, PERMIT_JOIN `0x22`.",
    "Message numbers are: HELLO/ACK `0x01/0x02`, PING/PONG `0x03/0x04`, snapshot `0x05..0x07`, INPUT_DESCRIPTOR `0x10`, MEASUREMENT `0x11`, SET_MEASUREMENT_POLICY `0x20`, CONFIG_RESULT `0x21`, PERMIT_JOIN `0x22`, COMMAND_REQUEST `0x30`, COMMAND_RESULT `0x31`.",
)
replace_once(
    "docs/GATEWAY_LINK_V2.md",
    "HELLO advertises `snapshot`, `measurement-policy`, `permit-join` and `capability-profile`.",
    "HELLO advertises `snapshot`, `measurement-policy`, `permit-join`, `capability-profile` and `commands`.",
)
insert_before(
    "docs/GATEWAY_LINK_V2.md",
    "MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload encodings remain otherwise unchanged from v1.",
    r'''`COMMAND_REQUEST` payload is `request_id:u32`, stable input reference, `kind:u8`, `value:f64`, `transition_ms:u32`. The first normalized command kind is `SET_ON_OFF`; it accepts value `0` or `1` and requires zero transition time. `COMMAND_RESULT` is `request_id:u32,status:u8` with `TRANSMITTED`, `UNSUPPORTED`, `INVALID` and `ERROR`.

`TRANSMITTED` means the Zigbee AF transmission confirmation completed successfully. It does **not** assert that the actuator applied the requested state. A subsequent normalized On/Off `MEASUREMENT` report remains the authoritative state observation. This distinction keeps transport acknowledgement separate from device state.

''',
)
replace_once(
    "docs/CONTINUATION.md",
    "- The next implementation slice adds normalized writable On/Off first, then Level Control, before building the second-C6 emulator profiles for deterministic round-trip testing.",
    "- Phase 5 adds normalized writable On/Off over GatewayLink v2. `COMMAND_RESULT=TRANSMITTED` is emitted only from the ezbee AF confirmation callback; the later normalized On/Off measurement remains authoritative device state.\n- The next implementation slice adds normalized Level Control, then the separate second-C6 emulator profiles for deterministic command/state round-trip testing.",
)
insert_after(
    "docs/ARCHITECTURE.md",
    "GatewayLink RX validates and enqueues source-neutral measurement policy requests but does not mutate the Zigbee registry. The Zigbee discovery task resolves the stable IEEE+endpoint identity, validates the endpoint's configurable capability, checks binding state and owns Configure Reporting submission. A correlated request completes only after the ZCL Configure Reporting response (or an explicit timeout/queue/route failure), which is normalized to `APPLIED`, `CLAMPED`, `UNSUPPORTED` or `ERROR` before crossing GatewayLink.\n",
    r'''

## Normalized command ownership

GatewayLink command requests carry a stable input identity, normalized command kind and source-neutral value. The Zigbee worker resolves IEEE+endpoint at execution time and validates the endpoint's `commandable` capability before translating the command to a standard ZCL request. For On/Off, the C6 uses the ezbee cluster-specific On/Off API with a bounded confirmation context. `COMMAND_RESULT=TRANSMITTED` reflects successful AF transmission confirmation only; normalized state reports remain authoritative device state. Raw cluster IDs and mutable short addresses never become application command identities.
''',
)

print("phase5 OnOff patch prepared")
