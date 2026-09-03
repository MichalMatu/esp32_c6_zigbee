from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, count))


def replace_between(path, start_marker, end_marker, new_text):
    p = Path(path)
    text = p.read_text()
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    p.write_text(text[:start] + new_text + text[end:])


# Common normalized capability profile.
replace(
    "main/gateway_inputs.h",
    "#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)\n\ntypedef struct {\n    gateway_measurement_kind_t kind;",
    "#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)\n\n#define GATEWAY_INPUT_METADATA_MAX_BYTES 24U\n\ntypedef struct {\n    gateway_input_capabilities_t readable;\n    gateway_input_capabilities_t reportable;\n    gateway_input_capabilities_t configurable;\n    gateway_input_capabilities_t commandable;\n} gateway_input_capability_profile_t;\n\ntypedef struct {\n    gateway_measurement_kind_t kind;",
)

replace(
    "main/gateway_zcl_value.h",
    "gateway_input_capabilities_t gateway_zcl_capabilities_for_server_cluster(\n    uint16_t cluster);\n\nbool gateway_zcl_normalize",
    "gateway_input_capabilities_t gateway_zcl_capabilities_for_server_cluster(\n    uint16_t cluster);\n\ngateway_input_capabilities_t gateway_zcl_capability_for_attribute(\n    uint16_t cluster, uint16_t attribute);\n\nbool gateway_zcl_normalize",
)

replace(
    "main/gateway_zcl_value.c",
    "bool gateway_zcl_normalize(uint16_t cluster,",
    r'''gateway_input_capabilities_t gateway_zcl_capability_for_attribute(
    uint16_t cluster, uint16_t attribute)
{
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG) {
        if (attribute == ZCL_ATTR_BATTERY_VOLTAGE) {
            return GATEWAY_INPUT_CAP_BATTERY_VOLTAGE;
        }
        if (attribute == ZCL_ATTR_BATTERY_PERCENT) {
            return GATEWAY_INPUT_CAP_BATTERY_PERCENT;
        }
        return 0U;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attribute == ZCL_ATTR_ON_OFF) {
        return GATEWAY_INPUT_CAP_ON_OFF;
    }
    if (attribute != ZCL_ATTR_MEASURED_VALUE) {
        return 0U;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_ILLUMINANCE;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_TEMPERATURE;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_HUMIDITY;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING) {
        return GATEWAY_INPUT_CAP_OCCUPANCY;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_CO2;
    }
    return 0U;
}

bool gateway_zcl_normalize(uint16_t cluster,''',
)

Path("main/gateway_zigbee_input.h").write_text(r'''#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_inputs.h"

gateway_input_capability_profile_t gateway_zigbee_capability_profile_from_clusters(
    const uint16_t *clusters, size_t count);

bool gateway_zigbee_stable_input_id(
    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,
    gateway_input_id_t *input);
''')

Path("main/gateway_zigbee_input.c").write_text(r'''#include "gateway_zigbee_input.h"

#include "gateway_reporting_policy.h"
#include "gateway_zcl_value.h"

#define ZCL_CLUSTER_ON_OFF 0x0006U

gateway_input_capability_profile_t gateway_zigbee_capability_profile_from_clusters(
    const uint16_t *clusters, size_t count)
{
    gateway_input_capability_profile_t profile = {0};
    if (clusters == NULL) {
        return profile;
    }
    for (size_t i = 0U; i < count; ++i) {
        const uint16_t cluster = clusters[i];
        profile.readable |= gateway_zcl_capabilities_for_server_cluster(cluster);

        gateway_reporting_spec_t spec;
        if (gateway_reporting_policy_spec(cluster, &spec)) {
            const gateway_input_capabilities_t capability =
                gateway_zcl_capability_for_attribute(cluster, spec.attribute_id);
            profile.reportable |= capability;
            profile.configurable |= capability;
        }
        if (cluster == ZCL_CLUSTER_ON_OFF) {
            profile.commandable |= GATEWAY_INPUT_CAP_ON_OFF;
        }
    }
    return profile;
}

bool gateway_zigbee_stable_input_id(
    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,
    gateway_input_id_t *input)
{
    if (input == NULL || ieee == NULL || !ieee_valid) {
        return false;
    }
    *input = gateway_input_make_zigbee(ieee, true, 0U, endpoint);
    return input->id[0] != '\0';
}
''')

replace(
    "main/gateway_device_state.h",
    "#define GATEWAY_BASIC_TEXT_MAX_BYTES 24U",
    "#define GATEWAY_BASIC_TEXT_MAX_BYTES GATEWAY_INPUT_METADATA_MAX_BYTES",
)
replace(
    "main/gateway_device_state.h",
    "    gateway_input_capabilities_t input_capabilities;\n",
    "    gateway_input_capability_profile_t input_profile;\n",
)

replace(
    "main/gateway_events.h",
    "        struct {\n            gateway_input_capabilities_t capabilities;\n            char model[24];\n        } input_desc;",
    "        struct {\n            gateway_input_capability_profile_t profile;\n            char manufacturer[GATEWAY_INPUT_METADATA_MAX_BYTES];\n            char model[GATEWAY_INPUT_METADATA_MAX_BYTES];\n        } input_desc;",
)

replace(
    "main/scd4x_input.c",
    "    event.data.input_desc.capabilities = SCD4X_CAPABILITIES;",
    "    event.data.input_desc.profile.readable = SCD4X_CAPABILITIES;",
)

# Zigbee endpoint profile generation and descriptor publication.
replace(
    "main/zigbee_gateway.c",
    "    if (slot == NULL || state == NULL || state->input_capabilities == 0U) {",
    "    if (slot == NULL || state == NULL || state->input_profile.readable == 0U) {",
)
replace(
    "main/zigbee_gateway.c",
    "    event.endpoint = state->endpoint;\n    event.data.input_desc.capabilities = state->input_capabilities;\n    strncpy(\n        event.data.input_desc.model, state->model,\n        sizeof(event.data.input_desc.model) - 1U);",
    "    event.endpoint = state->endpoint;\n    event.data.input_desc.profile = state->input_profile;\n    strncpy(\n        event.data.input_desc.manufacturer, state->manufacturer,\n        sizeof(event.data.input_desc.manufacturer) - 1U);\n    strncpy(\n        event.data.input_desc.model, state->model,\n        sizeof(event.data.input_desc.model) - 1U);",
)
replace(
    "main/zigbee_gateway.c",
    "    const gateway_input_capabilities_t capabilities =\n        gateway_zigbee_capabilities_from_clusters(\n            desc->app_cluster_list, desc->app_input_cluster_count);",
    "    const gateway_input_capability_profile_t profile =\n        gateway_zigbee_capability_profile_from_clusters(\n            desc->app_cluster_list, desc->app_input_cluster_count);",
)
replace(
    "main/zigbee_gateway.c",
    "    if (input_state != NULL && input_state->input_announced && capabilities == 0U) {",
    "    if (input_state != NULL && input_state->input_announced && profile.readable == 0U) {",
)
replace(
    "main/zigbee_gateway.c",
    "        input_state->input_capabilities = capabilities;",
    "        input_state->input_profile = profile;",
)
replace(
    "main/zigbee_gateway.c",
    "    if (input_state != NULL && capabilities != 0U) {",
    "    if (input_state != NULL && profile.readable != 0U) {",
)

replace(
    "main/gateway_transport.c",
    "        ESP_LOGI(TAG, \"input %s %s model=%s capabilities=0x%08\" PRIx32,\n                 event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ? \"available\" : \"unavailable\",\n                 input, event->data.input_desc.model,\n                 (uint32_t)event->data.input_desc.capabilities);",
    "        ESP_LOGI(TAG,\n                 \"input %s %s manufacturer=%s model=%s read=0x%08\" PRIx32\n                 \" report=0x%08\" PRIx32 \" config=0x%08\" PRIx32\n                 \" command=0x%08\" PRIx32,\n                 event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ? \"available\" : \"unavailable\",\n                 input, event->data.input_desc.manufacturer, event->data.input_desc.model,\n                 (uint32_t)event->data.input_desc.profile.readable,\n                 (uint32_t)event->data.input_desc.profile.reportable,\n                 (uint32_t)event->data.input_desc.profile.configurable,\n                 (uint32_t)event->data.input_desc.profile.commandable);",
)

# GatewayLink v2: no v1 shim. Framing and message numbers remain, descriptor semantics expand.
replace(
    "main/gateway_link_protocol.h",
    "#define GATEWAY_LINK_PROTOCOL_VERSION 1U",
    "#define GATEWAY_LINK_PROTOCOL_VERSION 2U",
)
replace(
    "main/gateway_link_protocol.h",
    "#define GATEWAY_LINK_MODEL_MAX_BYTES 24U",
    "#define GATEWAY_LINK_MANUFACTURER_MAX_BYTES 24U\n#define GATEWAY_LINK_MODEL_MAX_BYTES 24U",
)
replace(
    "main/gateway_link_protocol.h",
    "#define GATEWAY_LINK_FEATURE_PERMIT_JOIN        (1UL << 2)",
    "#define GATEWAY_LINK_FEATURE_PERMIT_JOIN        (1UL << 2)\n#define GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE (1UL << 3)",
)
replace(
    "main/gateway_link_protocol.h",
    "typedef struct {\n    gateway_input_id_t input;\n    bool available;\n    gateway_input_capabilities_t capabilities;\n    char model[GATEWAY_LINK_MODEL_MAX_BYTES];\n} gateway_link_input_descriptor_t;",
    "typedef struct {\n    gateway_input_id_t input;\n    bool available;\n    gateway_input_capability_profile_t profile;\n    char manufacturer[GATEWAY_LINK_MANUFACTURER_MAX_BYTES];\n    char model[GATEWAY_LINK_MODEL_MAX_BYTES];\n} gateway_link_input_descriptor_t;",
)
replace(
    "main/gateway_link_protocol.c",
    "_Static_assert(sizeof(double) == 8U, \"GatewayLink v1 requires 64-bit IEEE-754 double\");",
    "_Static_assert(sizeof(double) == 8U, \"GatewayLink v2 requires 64-bit IEEE-754 double\");",
)

replace_between(
    "main/gateway_link_protocol.c",
    "gateway_link_result_t gateway_link_encode_input_descriptor_payload(\n",
    "gateway_link_result_t gateway_link_encode_measurement_payload(\n",
    r'''gateway_link_result_t gateway_link_encode_input_descriptor_payload(
    const gateway_link_input_descriptor_t *descriptor,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (descriptor == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    size_t used = 0U;
    gateway_link_result_t result = encode_input_ref(&descriptor->input, payload, capacity, &used);
    if (result != GATEWAY_LINK_OK) return result;

    const size_t manufacturer_length = bounded_string_length(
        descriptor->manufacturer, GATEWAY_LINK_MANUFACTURER_MAX_BYTES);
    const size_t model_length = bounded_string_length(
        descriptor->model, GATEWAY_LINK_MODEL_MAX_BYTES);
    if (manufacturer_length >= GATEWAY_LINK_MANUFACTURER_MAX_BYTES ||
        model_length >= GATEWAY_LINK_MODEL_MAX_BYTES ||
        manufacturer_length > 255U || model_length > 255U) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    if (used + 19U + manufacturer_length + model_length > capacity) {
        return GATEWAY_LINK_BUFFER_TOO_SMALL;
    }

    payload[used++] = descriptor->available ? 1U : 0U;
    write_u32_le(&payload[used], descriptor->profile.readable);
    used += 4U;
    write_u32_le(&payload[used], descriptor->profile.reportable);
    used += 4U;
    write_u32_le(&payload[used], descriptor->profile.configurable);
    used += 4U;
    write_u32_le(&payload[used], descriptor->profile.commandable);
    used += 4U;

    payload[used++] = (uint8_t)manufacturer_length;
    if (manufacturer_length != 0U) {
        memcpy(&payload[used], descriptor->manufacturer, manufacturer_length);
        used += manufacturer_length;
    }
    payload[used++] = (uint8_t)model_length;
    if (model_length != 0U) {
        memcpy(&payload[used], descriptor->model, model_length);
        used += model_length;
    }
    *length = (uint16_t)used;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_input_descriptor_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_input_descriptor_t *descriptor)
{
    if (payload == NULL || descriptor == NULL) return GATEWAY_LINK_INVALID_ARG;
    memset(descriptor, 0, sizeof(*descriptor));
    size_t used = 0U;
    gateway_link_result_t result = decode_input_ref(payload, length, &descriptor->input, &used);
    if (result != GATEWAY_LINK_OK) return result;
    if (used + 19U > length) return GATEWAY_LINK_MALFORMED;
    if (payload[used] > 1U) return GATEWAY_LINK_MALFORMED;
    descriptor->available = payload[used++] != 0U;
    descriptor->profile.readable = read_u32_le(&payload[used]);
    used += 4U;
    descriptor->profile.reportable = read_u32_le(&payload[used]);
    used += 4U;
    descriptor->profile.configurable = read_u32_le(&payload[used]);
    used += 4U;
    descriptor->profile.commandable = read_u32_le(&payload[used]);
    used += 4U;

    const uint8_t manufacturer_length = payload[used++];
    if (manufacturer_length >= GATEWAY_LINK_MANUFACTURER_MAX_BYTES ||
        used + manufacturer_length + 1U > length) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (manufacturer_length != 0U) {
        memcpy(descriptor->manufacturer, &payload[used], manufacturer_length);
        used += manufacturer_length;
    }
    descriptor->manufacturer[manufacturer_length] = '\0';

    const uint8_t model_length = payload[used++];
    if (model_length >= GATEWAY_LINK_MODEL_MAX_BYTES || used + model_length != length) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (model_length != 0U) {
        memcpy(descriptor->model, &payload[used], model_length);
    }
    descriptor->model[model_length] = '\0';
    return GATEWAY_LINK_OK;
}

''',
)

replace(
    "main/gateway_link_event_adapter.c",
    "            .available = event->kind == GATEWAY_EVENT_INPUT_AVAILABLE,\n            .capabilities = event->data.input_desc.capabilities,",
    "            .available = event->kind == GATEWAY_EVENT_INPUT_AVAILABLE,\n            .profile = event->data.input_desc.profile,",
)
replace(
    "main/gateway_link_event_adapter.c",
    "        strncpy(descriptor.model, event->data.input_desc.model, sizeof(descriptor.model) - 1U);",
    "        strncpy(\n            descriptor.manufacturer, event->data.input_desc.manufacturer,\n            sizeof(descriptor.manufacturer) - 1U);\n        strncpy(descriptor.model, event->data.input_desc.model, sizeof(descriptor.model) - 1U);",
)

replace(
    "main/gateway_link_snapshot_cache.c",
    "    if (descriptor->capabilities != 0U || target->descriptor.capabilities == 0U) {\n        target->descriptor.capabilities = descriptor->capabilities;\n    }\n    if (descriptor->model[0] != '\\0') {",
    "    target->descriptor.profile = descriptor->profile;\n    if (descriptor->manufacturer[0] != '\\0') {\n        strncpy(target->descriptor.manufacturer,\n                descriptor->manufacturer,\n                sizeof(target->descriptor.manufacturer) - 1U);\n        target->descriptor.manufacturer[sizeof(target->descriptor.manufacturer) - 1U] = '\\0';\n    }\n    if (descriptor->model[0] != '\\0') {",
)

replace(
    "main/gateway_link_control.c",
    ".features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN,",
    ".features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN |\n            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,",
)

# Protocol tests.
replace(
    "tests/host/test_gateway_link_protocol.c",
    ".min_version = 1U,\n        .max_version = 1U,",
    ".min_version = GATEWAY_LINK_PROTOCOL_VERSION,\n        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,",
)
replace(
    "tests/host/test_gateway_link_protocol.c",
    "            GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |\n            GATEWAY_LINK_FEATURE_PERMIT_JOIN,",
    "            GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |\n            GATEWAY_LINK_FEATURE_PERMIT_JOIN |\n            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,",
)
replace(
    "tests/host/test_gateway_link_protocol.c",
    "    assert(decoded.min_version == 1U && decoded.max_version == 1U);",
    "    assert(decoded.min_version == GATEWAY_LINK_PROTOCOL_VERSION &&\n           decoded.max_version == GATEWAY_LINK_PROTOCOL_VERSION);",
)
replace(
    "tests/host/test_gateway_link_protocol.c",
    "    descriptor.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE |\n        GATEWAY_INPUT_CAP_HUMIDITY |\n        GATEWAY_INPUT_CAP_CO2;\n    strcpy(descriptor.model, \"SCD41\");",
    "    descriptor.profile.readable = GATEWAY_INPUT_CAP_TEMPERATURE |\n        GATEWAY_INPUT_CAP_HUMIDITY |\n        GATEWAY_INPUT_CAP_CO2;\n    descriptor.profile.reportable = GATEWAY_INPUT_CAP_TEMPERATURE;\n    descriptor.profile.configurable = GATEWAY_INPUT_CAP_TEMPERATURE;\n    strcpy(descriptor.manufacturer, \"Sensirion\");\n    strcpy(descriptor.model, \"SCD41\");",
)
replace(
    "tests/host/test_gateway_link_protocol.c",
    "    assert(decoded.capabilities == 0x13U);\n    assert(strcmp(decoded.model, \"SCD41\") == 0);",
    "    assert(decoded.profile.readable == 0x13U);\n    assert(decoded.profile.reportable == GATEWAY_INPUT_CAP_TEMPERATURE);\n    assert(decoded.profile.configurable == GATEWAY_INPUT_CAP_TEMPERATURE);\n    assert(decoded.profile.commandable == 0U);\n    assert(strcmp(decoded.manufacturer, \"Sensirion\") == 0);\n    assert(strcmp(decoded.model, \"SCD41\") == 0);",
)

# Event adapter tests.
replace(
    "tests/host/test_gateway_link_event_adapter.c",
    "    event.data.input_desc.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE |\n        GATEWAY_INPUT_CAP_HUMIDITY | GATEWAY_INPUT_CAP_CO2;\n    strcpy(event.data.input_desc.model, \"SCD41\");",
    "    event.data.input_desc.profile.readable = GATEWAY_INPUT_CAP_TEMPERATURE |\n        GATEWAY_INPUT_CAP_HUMIDITY | GATEWAY_INPUT_CAP_CO2;\n    event.data.input_desc.profile.reportable = GATEWAY_INPUT_CAP_TEMPERATURE;\n    event.data.input_desc.profile.configurable = GATEWAY_INPUT_CAP_TEMPERATURE;\n    strcpy(event.data.input_desc.manufacturer, \"Sensirion\");\n    strcpy(event.data.input_desc.model, \"SCD41\");",
)
replace(
    "tests/host/test_gateway_link_event_adapter.c",
    "    assert(decoded.capabilities == 0x13U);\n    assert(strcmp(decoded.model, \"SCD41\") == 0);",
    "    assert(decoded.profile.readable == 0x13U);\n    assert(decoded.profile.reportable == GATEWAY_INPUT_CAP_TEMPERATURE);\n    assert(decoded.profile.configurable == GATEWAY_INPUT_CAP_TEMPERATURE);\n    assert(strcmp(decoded.manufacturer, \"Sensirion\") == 0);\n    assert(strcmp(decoded.model, \"SCD41\") == 0);",
)

# Snapshot tests.
replace(
    "tests/host/test_gateway_link_snapshot_cache.c",
    "    value.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_CO2;\n    strcpy(value.model, \"sensor-model\");",
    "    value.profile.readable = GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_CO2;\n    value.profile.reportable = GATEWAY_INPUT_CAP_TEMPERATURE;\n    value.profile.configurable = GATEWAY_INPUT_CAP_TEMPERATURE;\n    strcpy(value.manufacturer, \"sensor-maker\");\n    strcpy(value.model, \"sensor-model\");",
)
replace(
    "tests/host/test_gateway_link_snapshot_cache.c",
    "    assert(strcmp(copied.model, \"sensor-model\") == 0);",
    "    assert(copied.profile.readable ==\n           (GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_CO2));\n    assert(copied.profile.reportable == GATEWAY_INPUT_CAP_TEMPERATURE);\n    assert(strcmp(copied.manufacturer, \"sensor-maker\") == 0);\n    assert(strcmp(copied.model, \"sensor-model\") == 0);",
    1,
)

# Control tests: v2 only and truthful feature advertisement.
replace_between(
    "tests/host/test_gateway_link_control.c",
    "static void test_hello_compatibility(void)\n",
    "static void test_hello_builders_truthfully_advertise_permit_join(void)\n",
    r'''static void test_hello_compatibility(void)
{
    gateway_link_frame_t frame = make_hello(
        GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_S3_HOST,
        GATEWAY_LINK_PROTOCOL_VERSION, GATEWAY_LINK_PROTOCOL_VERSION);
    gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_HELLO_ACK);
    assert(gateway_link_control_peer_compatible(&action.peer_hello));

    frame = make_hello(GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_S3_HOST, 1U, 1U);
    action = gateway_link_control_parse(&frame);
    assert(!gateway_link_control_peer_compatible(&action.peer_hello));

    frame = make_hello(
        GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_C6_GATEWAY,
        GATEWAY_LINK_PROTOCOL_VERSION, GATEWAY_LINK_PROTOCOL_VERSION);
    action = gateway_link_control_parse(&frame);
    assert(!gateway_link_control_peer_compatible(&action.peer_hello));
}

''',
)
replace(
    "tests/host/test_gateway_link_control.c",
    "    assert(hello.features == (GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN));",
    "    assert(hello.features == (GATEWAY_LINK_FEATURE_SNAPSHOT |\n        GATEWAY_LINK_FEATURE_PERMIT_JOIN | GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE));",
    2,
)

# E2E descriptor helper and feature checks.
replace(
    "tests/host/test_gateway_link_e2e.c",
    ".features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN,",
    ".features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN |\n            GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE,",
)
replace(
    "tests/host/test_gateway_link_e2e.c",
    "    assert((c6_hello.features & GATEWAY_LINK_FEATURE_PERMIT_JOIN) != 0U);\n    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) == 0U);",
    "    assert((c6_hello.features & GATEWAY_LINK_FEATURE_PERMIT_JOIN) != 0U);\n    assert((c6_hello.features & GATEWAY_LINK_FEATURE_CAPABILITY_PROFILE) != 0U);\n    assert((c6_hello.features & GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY) == 0U);",
)
replace(
    "tests/host/test_gateway_link_e2e.c",
    "        .available = true,\n        .capabilities = capabilities,",
    "        .available = true,\n        .profile = {.readable = capabilities},",
)
replace(
    "tests/host/test_gateway_link_e2e.c",
    "            assert(decoded.available);\n            ++descriptors_seen;",
    "            assert(decoded.available);\n            assert(decoded.profile.readable != 0U);\n            ++descriptors_seen;",
)

# Zigbee capability profile tests.
Path("tests/host/test_gateway_zigbee_input.c").write_text(r'''#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_zigbee_input.h"

static void test_capability_profile(void)
{
    const uint16_t clusters[] = {
        0x0000U, 0x0001U, 0x0006U, 0x0402U, 0x0405U, 0x0b04U,
    };
    const gateway_input_capability_profile_t profile =
        gateway_zigbee_capability_profile_from_clusters(
            clusters, sizeof(clusters) / sizeof(clusters[0]));
    const gateway_input_capabilities_t expected_readable =
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE |
        GATEWAY_INPUT_CAP_BATTERY_PERCENT |
        GATEWAY_INPUT_CAP_ON_OFF |
        GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY;
    const gateway_input_capabilities_t expected_reporting =
        GATEWAY_INPUT_CAP_BATTERY_PERCENT |
        GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY;
    assert(profile.readable == expected_readable);
    assert(profile.reportable == expected_reporting);
    assert(profile.configurable == expected_reporting);
    assert(profile.commandable == GATEWAY_INPUT_CAP_ON_OFF);

    const gateway_input_capability_profile_t empty =
        gateway_zigbee_capability_profile_from_clusters(NULL, 3U);
    assert(empty.readable == 0U);
    assert(empty.reportable == 0U);
    assert(empty.configurable == 0U);
    assert(empty.commandable == 0U);
}

static void test_stable_ieee_identity(void)
{
    const uint8_t ieee[8] = {0xdd, 0xcc, 0xbb, 0xaa, 0x00, 0x4b, 0x12, 0x00};
    gateway_input_id_t input = {0};
    assert(!gateway_zigbee_stable_input_id(ieee, false, 1U, &input));
    assert(gateway_zigbee_stable_input_id(ieee, true, 7U, &input));
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 7U);
    assert(strcmp(input.id, "zigbee:00124b00aabbccdd") == 0);
}

int main(void)
{
    test_capability_profile();
    test_stable_ieee_identity();
    puts("gateway_zigbee_input host tests passed");
    return 0;
}
''')

# Exact attribute capability coverage.
replace(
    "tests/host/test_gateway_zcl_value.c",
    "static void test_co2_and_invalid_input(void)",
    r'''static void test_attribute_capabilities(void)
{
    assert(gateway_zcl_capability_for_attribute(CLUSTER_POWER_CONFIG, 0x0020U) ==
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_POWER_CONFIG, 0x0021U) ==
        GATEWAY_INPUT_CAP_BATTERY_PERCENT);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_ON_OFF, 0x0000U) ==
        GATEWAY_INPUT_CAP_ON_OFF);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_TEMPERATURE, 0x0000U) ==
        GATEWAY_INPUT_CAP_TEMPERATURE);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_HUMIDITY, 0x0000U) ==
        GATEWAY_INPUT_CAP_HUMIDITY);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_POWER_CONFIG, 0xffffU) == 0U);
}

static void test_co2_and_invalid_input(void)''',
)
replace(
    "tests/host/test_gateway_zcl_value.c",
    "    test_boolean_values();\n    test_co2_and_invalid_input();",
    "    test_boolean_values();\n    test_attribute_capabilities();\n    test_co2_and_invalid_input();",
)

# CI must keep both profile dependency and source hygiene gate live.
replace(
    ".github/workflows/quality.yml",
    "jobs:\n  host-zcl:",
    "jobs:\n  source-hygiene:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Reject embedded NUL bytes in text sources\n        run: python3 tests/host/test_no_embedded_nul.py\n\n  host-zcl:",
)
replace(
    ".github/workflows/quality.yml",
    "            main/gateway_zigbee_input.c \\\n            main/gateway_zcl_value.c \\\n            main/gateway_inputs.c \\\n",
    "            main/gateway_zigbee_input.c \\\n            main/gateway_zcl_value.c \\\n            main/gateway_reporting_policy.c \\\n            main/gateway_inputs.c \\\n",
)

# Current development contract documentation. v1 remains historical at the frozen tag.
Path("docs/GATEWAY_LINK_V2.md").write_text(r'''# GatewayLink v2

GatewayLink v2 is the current development contract between the ESP32-C6 gateway and the future application host. It carries normalized source-neutral identity, capability access, availability and measurements. Standard Zigbee cluster IDs remain C6 implementation detail and are not required by the application host.

GatewayLink v1 remains documented in `docs/GATEWAY_LINK_V1.md` and frozen by `c6-gatewaylink-stable-2026-09-03`. The active branch does not implement a v1 compatibility shim because there is no deployed S3 peer or persisted application flow requiring migration.

## Framing and physical backends

Framing is unchanged from v1: a COBS-encoded binary packet terminated by `0x00`, little-endian fields, IEEE CRC32, maximum encoded frame size 256 bytes and maximum payload 220 bytes. The decoded header is `GL`, protocol version `2`, message type, flags, reserved zero byte, sender-local sequence, payload length, payload and CRC32.

UART remains the known-working default physical backend. The selectable C6 I2C mailbox backend transports the same complete encoded GatewayLink frame; changing UART to I2C does not change the normalized data model.

## Stable input reference

Every input-bearing payload starts with `source:u8, channel:u8, id_length:u8, id:N`. Zigbee input IDs use authoritative IEEE identity plus endpoint. Zigbee short addresses are mutable routes and are never normalized application identities.

## Capability profile

`INPUT_DESCRIPTOR` exposes four independent source-neutral capability masks:

- `readable`: normalized values/state that C6 understands for this input;
- `reportable`: values for which C6 has a defined reporting path;
- `configurable`: values for which C6 can translate a reporting/configuration policy;
- `commandable`: values/state for which C6 has a defined write/command path.

A bit is advertised only when C6 has an explicit standard implementation. Manufacturer-specific or Tuya-style behavior must not silently set generic bits.

At this phase, standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable`; the actual command request/result path is added in the following implementation phase. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy yet.

## INPUT_DESCRIPTOR payload

After the stable input reference:

`available:u8, readable:u32, reportable:u32, configurable:u32, commandable:u32, manufacturer_length:u8, manufacturer:N, model_length:u8, model:N`.

Manufacturer and model are bounded to 23 data bytes each plus local NUL termination after decode. Another descriptor for the same stable input replaces its current normalized capability profile and availability in the host registry.

## Other messages

Message numbers remain: HELLO/ACK `0x01/0x02`, PING/PONG `0x03/0x04`, snapshot `0x05..0x07`, INPUT_DESCRIPTOR `0x10`, MEASUREMENT `0x11`, SET_MEASUREMENT_POLICY `0x20`, CONFIG_RESULT `0x21`, PERMIT_JOIN `0x22`.

HELLO advertises the `snapshot`, `permit-join` and `capability-profile` feature bits. `measurement-policy` remains unadvertised until the next phase connects the request to real Zigbee Configure Reporting state.

MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload semantics remain otherwise unchanged from v1. A peer must negotiate protocol version 2; the active branch intentionally does not decode v1 frames.
''')

replace(
    "README.md",
    "The protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements and versioned controls.",
    "The current protocol-neutral C6-to-S3 development contract is specified in [docs/GATEWAY_LINK_V2.md](docs/GATEWAY_LINK_V2.md). GatewayLink v2 uses bounded binary COBS frames with CRC32 and carries stable input identity, normalized capability access profiles, descriptors, normalized measurements and versioned controls. The frozen v1 contract remains documented for the `c6-gatewaylink-stable-2026-09-03` recovery point; the active branch has no v1 compatibility shim because there is no deployed S3 peer to migrate.",
)
replace(
    "README.md",
    "After a successful Simple Descriptor, an endpoint with at least one actually normalized server cluster also publishes a protocol-neutral `INPUT_AVAILABLE` descriptor. Capabilities are derived from the same ZCL normalization support table, and the descriptor is emitted only after an authoritative IEEE identity is known; provisional `zigbee-short:*` identities are never exposed on GatewayLink.",
    "After a successful Simple Descriptor, an endpoint with at least one actually normalized server cluster also publishes a protocol-neutral `INPUT_AVAILABLE` descriptor. Its v2 capability profile separates readable normalized values from reportable/configurable values and commandable state, so the future S3 does not need raw Zigbee cluster knowledge. The descriptor also carries bounded Basic manufacturer/model metadata when known and is emitted only after an authoritative IEEE identity is known; provisional short-address identities are never exposed on GatewayLink.",
)
replace(
    "docs/CONTINUATION.md",
    "- Phase 2 replaces the fixed reporting/binding bitmaps with bounded records keyed by endpoint/cluster/attribute so Configure Reporting state can scale beyond one hard-coded bit per supported cluster and preserve per-attribute result status.\n- The next implementation slice is normalized capability/configuration exposure through GatewayLink, followed by frontend-driven Configure Reporting and writable commands.",
    "- Phase 2 is complete: fixed reporting/binding bitmaps were replaced with bounded records keyed by endpoint/cluster/attribute so Configure Reporting state preserves per-attribute status.\n- Phase 3 introduces the normalized capability access profile and GatewayLink v2: readable, reportable, configurable and commandable masks plus manufacturer/model metadata are carried without exposing raw Zigbee semantics to the future S3. There is intentionally no v1 shim on the active branch.\n- The next implementation slice connects `SET_MEASUREMENT_POLICY` to real Zigbee Configure Reporting and returns normalized per-request results, followed by writable On/Off and Level commands.",
)

with Path("docs/ARCHITECTURE.md").open("a") as f:
    f.write(r'''

## Normalized capability access profile

The application boundary uses `gateway_input_capability_profile_t`, not Zigbee cluster IDs. `readable` identifies normalized state/measurements C6 understands, `reportable` and `configurable` identify values backed by an explicit source policy implementation, and `commandable` identifies normalized writable state. Zigbee Simple Descriptor data remains internal evidence used to construct that profile. GatewayLink v2 transports the profile unchanged over either UART or I2C.
''')

print("phase3 patch prepared")
