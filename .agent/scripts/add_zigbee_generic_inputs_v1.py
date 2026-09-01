from pathlib import Path

ROOT = Path('.')

zigbee_h = r'''#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_inputs.h"

gateway_input_capabilities_t gateway_zigbee_capabilities_from_clusters(
    const uint16_t *clusters, size_t count);

bool gateway_zigbee_stable_input_id(
    const uint8_t ieee[8], bool ieee_valid, uint8_t endpoint,
    gateway_input_id_t *input);
'''

zigbee_c = r'''#include "gateway_zigbee_input.h"

#include "gateway_zcl_value.h"

gateway_input_capabilities_t gateway_zigbee_capabilities_from_clusters(
    const uint16_t *clusters, size_t count)
{
    gateway_input_capabilities_t capabilities = 0U;
    if (clusters == NULL) {
        return capabilities;
    }
    for (size_t i = 0U; i < count; ++i) {
        capabilities |= gateway_zcl_capabilities_for_server_cluster(clusters[i]);
    }
    return capabilities;
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
'''

test_zigbee = r'''#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_zigbee_input.h"

static void test_capabilities(void)
{
    const uint16_t clusters[] = {
        0x0000U, 0x0001U, 0x0402U, 0x0405U, 0x0b04U,
    };
    const gateway_input_capabilities_t expected =
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE |
        GATEWAY_INPUT_CAP_BATTERY_PERCENT |
        GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY;
    assert(gateway_zigbee_capabilities_from_clusters(
        clusters, sizeof(clusters) / sizeof(clusters[0])) == expected);
    assert(gateway_zigbee_capabilities_from_clusters(NULL, 3U) == 0U);
}

static void test_stable_ieee_identity(void)
{
    const uint8_t ieee[8] = {0x00, 0x12, 0x4b, 0x00, 0xaa, 0xbb, 0xcc, 0xdd};
    gateway_input_id_t input = {0};
    assert(!gateway_zigbee_stable_input_id(ieee, false, 1U, &input));
    assert(gateway_zigbee_stable_input_id(ieee, true, 7U, &input));
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 7U);
    assert(strcmp(input.id, "zigbee:00124b00aabbccdd") == 0);
}

int main(void)
{
    test_capabilities();
    test_stable_ieee_identity();
    puts("gateway_zigbee_input host tests passed");
    return 0;
}
'''

(ROOT / 'main/gateway_zigbee_input.h').write_text(zigbee_h)
(ROOT / 'main/gateway_zigbee_input.c').write_text(zigbee_c)
(ROOT / 'tests/host/test_gateway_zigbee_input.c').write_text(test_zigbee)

# Expose normalization capabilities from the same source of truth as value decoding.
h = ROOT / 'main/gateway_zcl_value.h'
s = h.read_text()
needle = 'uint16_t gateway_zcl_attr_size(uint8_t type, const void *value);\n\n'
assert needle in s
s = s.replace(needle, needle + 'gateway_input_capabilities_t gateway_zcl_capabilities_for_server_cluster(\n    uint16_t cluster);\n\n', 1)
h.write_text(s)

c = ROOT / 'main/gateway_zcl_value.c'
s = c.read_text()
needle = 'bool gateway_zcl_normalize(uint16_t cluster,\n'
assert needle in s
cap_fn = r'''gateway_input_capabilities_t gateway_zcl_capabilities_for_server_cluster(
    uint16_t cluster)
{
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG) {
        return GATEWAY_INPUT_CAP_BATTERY_VOLTAGE |
            GATEWAY_INPUT_CAP_BATTERY_PERCENT;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF) {
        return GATEWAY_INPUT_CAP_ON_OFF;
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

'''
s = s.replace(needle, cap_fn + needle, 1)
c.write_text(s)

# Endpoint state retains only normalized-input metadata required for lifecycle replay.
p = ROOT / 'main/gateway_device_state.h'
s = p.read_text()
needle = '    uint32_t binding_requested_at_ms;\n'
assert needle in s
s = s.replace(needle, needle + '    gateway_input_capabilities_t input_capabilities;\n    bool input_announced;\n', 1)
p.write_text(s)

# Add firmware source.
p = ROOT / 'main/CMakeLists.txt'
s = p.read_text()
needle = '        "gateway_zcl_value.c"\n'
assert needle in s
s = s.replace(needle, needle + '        "gateway_zigbee_input.c"\n', 1)
p.write_text(s)

# Zigbee SDK shell now emits protocol-neutral descriptors only for stable IEEE identities.
p = ROOT / 'main/zigbee_gateway.c'
s = p.read_text()
needle = '#include "gateway_zcl_value.h"\n'
assert needle in s
s = s.replace(needle, needle + '#include "gateway_zigbee_input.h"\n', 1)

needle = 'static void simple_callback(\n'
assert needle in s
helper = r'''static bool publish_generic_input(
    device_slot_t *slot, endpoint_state_t *state, bool available)
{
    if (slot == NULL || state == NULL || state->input_capabilities == 0U) {
        return false;
    }
    gateway_input_id_t input;
    if (!gateway_zigbee_stable_input_id(
            slot->device.ieee, slot->device.ieee_valid,
            state->endpoint, &input)) {
        return false;
    }
    gateway_event_t event = gateway_event_make_input(
        available ? GATEWAY_EVENT_INPUT_AVAILABLE : GATEWAY_EVENT_INPUT_UNAVAILABLE,
        &input);
    event.endpoint = state->endpoint;
    event.data.input_desc.capabilities = state->input_capabilities;
    if (!gateway_event_publish(&event)) {
        return false;
    }
    state->input_announced = available;
    return true;
}

'''
s = s.replace(needle, helper + needle, 1)

old = '''    const ezb_af_simple_desc_t *desc = &result->rsp->desc;\n    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_ENDPOINT, &slot->device);\n'''
new = '''    const ezb_af_simple_desc_t *desc = &result->rsp->desc;\n    endpoint_state_t *input_state = endpoint_state(slot, desc->ep_id, true);\n    const gateway_input_capabilities_t capabilities =\n        gateway_zigbee_capabilities_from_clusters(\n            desc->app_cluster_list, desc->app_input_cluster_count);\n    if (!slot->device.ieee_valid) {\n        ezb_extaddr_t resolved_ieee;\n        if (ezb_address_extended_by_short(slot->device.short_addr, &resolved_ieee) ==\n            EZB_ERR_NONE) {\n            memcpy(slot->device.ieee, resolved_ieee.u8, sizeof(slot->device.ieee));\n            slot->device.ieee_valid = true;\n        }\n    }\n    if (input_state != NULL && input_state->input_announced && capabilities == 0U) {\n        (void)publish_generic_input(slot, input_state, false);\n    }\n    if (input_state != NULL) {\n        input_state->input_capabilities = capabilities;\n    }\n\n    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_ENDPOINT, &slot->device);\n'''
assert old in s
s = s.replace(old, new, 1)

needle = '    gateway_event_publish(&event);\n\n    bool basic = false;\n'
assert needle in s
s = s.replace(needle, '    gateway_event_publish(&event);\n    if (input_state != NULL && capabilities != 0U) {\n        (void)publish_generic_input(slot, input_state, true);\n    }\n\n    bool basic = false;\n', 1)

needle = 'static void device_left(\n'
assert needle in s
unavail = r'''static void publish_slot_inputs_unavailable(device_slot_t *slot)
{
    if (slot == NULL || !slot->device.ieee_valid) {
        return;
    }
    for (size_t i = 0U; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
        endpoint_state_t *state = &slot->endpoints[i];
        if (state->in_use && state->input_announced) {
            (void)publish_generic_input(slot, state, false);
        }
    }
}

'''
s = s.replace(needle, unavail + needle, 1)

needle = '''    gateway_event_publish(&event);\n\n    if (slot == NULL) {\n        return;\n    }\n    if (kind == GATEWAY_EVENT_DEVICE_LEAVE_RESET) {\n'''
replacement = '''    gateway_event_publish(&event);\n\n    if (slot == NULL) {\n        return;\n    }\n    if (kind == GATEWAY_EVENT_DEVICE_LEAVE_RESET ||\n        kind == GATEWAY_EVENT_DEVICE_LEAVE_REJOIN) {\n        publish_slot_inputs_unavailable(slot);\n    }\n    if (kind == GATEWAY_EVENT_DEVICE_LEAVE_RESET) {\n'''
assert needle in s
s = s.replace(needle, replacement, 1)
p.write_text(s)

# Add CI job.
p = ROOT / '.github/workflows/quality.yml'
s = p.read_text()
marker = '  host-link-protocol:\n'
assert marker in s
job = '''  host-zigbee-input:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Build Zigbee input adapter host tests\n        run: |\n          cc -std=c11 -Wall -Wextra -Werror -pedantic \\\n            -DGATEWAY_ZCL_HOST_TEST \\\n            -Imain \\\n            tests/host/test_gateway_zigbee_input.c \\\n            main/gateway_zigbee_input.c \\\n            main/gateway_zcl_value.c \\\n            main/gateway_inputs.c \\\n            -lm \\\n            -o /tmp/test_gateway_zigbee_input\n      - name: Run Zigbee input adapter host tests\n        run: /tmp/test_gateway_zigbee_input\n\n'''
s = s.replace(marker, job + marker, 1)
p.write_text(s)

# Documentation: describe the new stable generic descriptor lifecycle.
p = ROOT / 'docs/ARCHITECTURE.md'
s = p.read_text()
needle = '- `gateway_zcl_value.c/.h` is a pure, host-testable ZCL attribute normalization layer. Unsupported or scaling-dependent data stays raw instead of being guessed.\n'
assert needle in s
s = s.replace(needle, needle + '- `gateway_zigbee_input.c/.h` is the pure Zigbee-to-generic input adapter for capability aggregation and stable IEEE-based input identity. It deliberately refuses provisional short-address identities.\n', 1)
p.write_text(s)

p = ROOT / 'README.md'
s = p.read_text()
needle = 'Recognized report attributes are normalized only when their incoming cluster, attribute, and ZCL type agree and the value is not a Zigbee invalid/sentinel value: temperature, humidity, illuminance, occupancy, CO2, battery voltage/percentage, and On/Off. Electrical Measurement and Metering reports are deliberately emitted through the raw fallback for now: their integer values are not presented as volts, amps, watts, or kWh until the required per-endpoint multiplier/divisor attributes are cached.\n'
assert needle in s
s = s.replace(needle, needle + '\nAfter a successful Simple Descriptor, an endpoint with at least one actually normalized server cluster also publishes a protocol-neutral `INPUT_AVAILABLE` descriptor. Capabilities are derived from the same ZCL normalization support table, and the descriptor is emitted only after an authoritative IEEE identity is known; provisional `zigbee-short:*` identities are never exposed on GatewayLink. A known RESET leave and a known REJOIN leave publish `INPUT_UNAVAILABLE` for previously announced endpoints; the generic descriptor is re-announced after rediscovery. `DEVICE_UNAVAILABLE` and unknown leave/update signals remain non-authoritative and do not fabricate generic offline state.\n', 1)
p.write_text(s)

p = ROOT / 'docs/GATEWAY_LINK_V1.md'
s = p.read_text()
needle = 'For example the validated local sensor is `source=2`, `channel=0`, `id=scd4x:a12bef073b43`. A Zigbee adapter should expose the authoritative IEEE-based identity before publishing it over GatewayLink; mutable short addresses are not application identities.\n'
assert needle in s
s = s.replace(needle, needle + ' The C6 Zigbee adapter enforces this rule: supported endpoints are announced only after IEEE recovery succeeds, and known reset/rejoin leaves emit protocol-neutral unavailability for those stable identities.\n', 1)
p.write_text(s)

for path in [ROOT / 'main/gateway_zigbee_input.h', ROOT / 'main/gateway_zigbee_input.c', ROOT / 'tests/host/test_gateway_zigbee_input.c']:
    assert b'\x00' not in path.read_bytes()
