from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    if old not in text:
        raise SystemExit(f"missing match in {path}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def insert_before(path, marker, addition):
    text = read(path)
    if marker not in text:
        raise SystemExit(f"missing marker in {path}: {marker[:120]!r}")
    write(path, text.replace(marker, addition + marker, 1))

# Normalized Level measurement/capability/command.
replace_once(
    "main/gateway_inputs.h",
    "    GATEWAY_MEAS_ON_OFF,\n} gateway_measurement_kind_t;",
    "    GATEWAY_MEAS_ON_OFF,\n    GATEWAY_MEAS_LEVEL,\n} gateway_measurement_kind_t;",
)
replace_once(
    "main/gateway_inputs.h",
    "    GATEWAY_COMMAND_SET_ON_OFF = 0,\n} gateway_command_kind_t;",
    "    GATEWAY_COMMAND_SET_ON_OFF = 0,\n    GATEWAY_COMMAND_SET_LEVEL = 1,\n} gateway_command_kind_t;",
)
replace_once(
    "main/gateway_inputs.h",
    "#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)\n",
    "#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)\n#define GATEWAY_INPUT_CAP_LEVEL           (1UL << 13)\n",
)
replace_once(
    "main/gateway_inputs.c",
    "        GATEWAY_INPUT_CAP_ON_OFF,\n    };",
    "        GATEWAY_INPUT_CAP_ON_OFF,\n        GATEWAY_INPUT_CAP_LEVEL,\n    };",
)

# Pure Level command plan. Values are normalized percent; transitions must be exact 100 ms ticks.
replace_once(
    "main/gateway_command_policy.h",
    "    gateway_input_capabilities_t capability;\n    bool target_on;\n} gateway_command_plan_t;",
    "    gateway_input_capabilities_t capability;\n    gateway_command_kind_t kind;\n    bool target_on;\n    uint8_t level;\n    uint16_t transition_time;\n} gateway_command_plan_t;",
)
write("main/gateway_command_policy.c", r'''#include "gateway_command_policy.h"

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
''')

insert_before(
    "tests/host/test_gateway_command_policy.c",
    "static void test_invalid_on_off_values(void)\n",
    r'''static void test_level_plan(void)
{
    gateway_command_plan_t plan;
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 50.0, 1200U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(plan.kind == GATEWAY_COMMAND_SET_LEVEL);
    assert(plan.cluster_id == 0x0008U);
    assert(plan.capability == GATEWAY_INPUT_CAP_LEVEL);
    assert(plan.level == 127U);
    assert(plan.transition_time == 12U);

    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 100.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(plan.level == 254U);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, -1.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 101.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 50.0, 150U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
}

''',
)
replace_once(
    "tests/host/test_gateway_command_policy.c",
    "    test_on_off_plan();\n    test_invalid_on_off_values();",
    "    test_on_off_plan();\n    test_level_plan();\n    test_invalid_on_off_values();",
)

# Standard ZCL CurrentLevel normalization.
replace_once(
    "main/gateway_zcl_value.c",
    "#define EZB_ZCL_CLUSTER_ID_ON_OFF 0x0006U\n",
    "#define EZB_ZCL_CLUSTER_ID_ON_OFF 0x0006U\n#define EZB_ZCL_CLUSTER_ID_LEVEL_CONTROL 0x0008U\n",
)
replace_once(
    "main/gateway_zcl_value.c",
    "#define ZCL_ATTR_ON_OFF 0x0000U\n",
    "#define ZCL_ATTR_ON_OFF 0x0000U\n#define ZCL_ATTR_CURRENT_LEVEL 0x0000U\n",
)
replace_once(
    "main/gateway_zcl_value.c",
    "    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF) {\n        return GATEWAY_INPUT_CAP_ON_OFF;\n    }",
    "    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF) {\n        return GATEWAY_INPUT_CAP_ON_OFF;\n    }\n    if (cluster == EZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {\n        return GATEWAY_INPUT_CAP_LEVEL;\n    }",
)
replace_once(
    "main/gateway_zcl_value.c",
    "    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attribute == ZCL_ATTR_ON_OFF) {\n        return GATEWAY_INPUT_CAP_ON_OFF;\n    }",
    "    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attribute == ZCL_ATTR_ON_OFF) {\n        return GATEWAY_INPUT_CAP_ON_OFF;\n    }\n    if (cluster == EZB_ZCL_CLUSTER_ID_LEVEL_CONTROL &&\n        attribute == ZCL_ATTR_CURRENT_LEVEL) {\n        return GATEWAY_INPUT_CAP_LEVEL;\n    }",
)
insert_before(
    "main/gateway_zcl_value.c",
    "    return false;\n}",
    r'''    if (cluster == EZB_ZCL_CLUSTER_ID_LEVEL_CONTROL &&
        attribute == ZCL_ATTR_CURRENT_LEVEL &&
        read_u8(value, type, &u8) && u8 <= 254U) {
        *kind = GATEWAY_MEAS_LEVEL;
        *unit = GATEWAY_UNIT_PERCENT;
        *number = (double)u8 * 100.0 / 254.0;
        return true;
    }
''',
)

replace_once(
    "tests/host/test_gateway_zcl_value.c",
    "#define CLUSTER_ON_OFF 0x0006U\n",
    "#define CLUSTER_ON_OFF 0x0006U\n#define CLUSTER_LEVEL_CONTROL 0x0008U\n",
)
insert_before(
    "tests/host/test_gateway_zcl_value.c",
    "static void test_attribute_capabilities(void)\n",
    r'''static void test_level(void)
{
    uint8_t raw = 127U;
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;
    assert(gateway_zcl_normalize(
        CLUSTER_LEVEL_CONTROL, 0x0000U, TYPE_UINT8, &raw, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_LEVEL);
    assert(unit == GATEWAY_UNIT_PERCENT);
    expect_close(value, 50.0);
    raw = 0xffU;
    assert(!gateway_zcl_normalize(
        CLUSTER_LEVEL_CONTROL, 0x0000U, TYPE_UINT8, &raw, &kind, &unit, &value));
}

''',
)
replace_once(
    "tests/host/test_gateway_zcl_value.c",
    "    assert(gateway_zcl_capability_for_attribute(CLUSTER_ON_OFF, 0x0000U) ==\n        GATEWAY_INPUT_CAP_ON_OFF);",
    "    assert(gateway_zcl_capability_for_attribute(CLUSTER_ON_OFF, 0x0000U) ==\n        GATEWAY_INPUT_CAP_ON_OFF);\n    assert(gateway_zcl_capability_for_attribute(CLUSTER_LEVEL_CONTROL, 0x0000U) ==\n        GATEWAY_INPUT_CAP_LEVEL);",
)
replace_once(
    "tests/host/test_gateway_zcl_value.c",
    "    test_boolean_values();\n    test_attribute_capabilities();",
    "    test_boolean_values();\n    test_level();\n    test_attribute_capabilities();",
)

# Capability profile marks Level server clusters readable + commandable.
replace_once(
    "main/gateway_zigbee_input.c",
    "#define ZCL_CLUSTER_ON_OFF 0x0006U\n",
    "#define ZCL_CLUSTER_ON_OFF 0x0006U\n#define ZCL_CLUSTER_LEVEL_CONTROL 0x0008U\n",
)
replace_once(
    "main/gateway_zigbee_input.c",
    "        if (cluster == ZCL_CLUSTER_ON_OFF) {\n            profile.commandable |= GATEWAY_INPUT_CAP_ON_OFF;\n        }",
    "        if (cluster == ZCL_CLUSTER_ON_OFF) {\n            profile.commandable |= GATEWAY_INPUT_CAP_ON_OFF;\n        }\n        if (cluster == ZCL_CLUSTER_LEVEL_CONTROL) {\n            profile.commandable |= GATEWAY_INPUT_CAP_LEVEL;\n        }",
)
replace_once(
    "tests/host/test_gateway_zigbee_input.c",
    "        0x0006U,\n",
    "        0x0006U,\n        0x0008U,\n",
)
replace_once(
    "tests/host/test_gateway_zigbee_input.c",
    "    assert((profile.commandable & GATEWAY_INPUT_CAP_ON_OFF) != 0U);\n",
    "    assert((profile.commandable & GATEWAY_INPUT_CAP_ON_OFF) != 0U);\n    assert((profile.readable & GATEWAY_INPUT_CAP_LEVEL) != 0U);\n    assert((profile.commandable & GATEWAY_INPUT_CAP_LEVEL) != 0U);\n",
)

# Explicit wire mappings for new measurement and command kind.
replace_once(
    "main/gateway_link_protocol.c",
    "    case GATEWAY_MEAS_ON_OFF: *wire = 13U; break;\n",
    "    case GATEWAY_MEAS_ON_OFF: *wire = 13U; break;\n    case GATEWAY_MEAS_LEVEL: *wire = 14U; break;\n",
)
replace_once(
    "main/gateway_link_protocol.c",
    "    case 13U: *kind = GATEWAY_MEAS_ON_OFF; break;\n",
    "    case 13U: *kind = GATEWAY_MEAS_ON_OFF; break;\n    case 14U: *kind = GATEWAY_MEAS_LEVEL; break;\n",
)
replace_once(
    "main/gateway_link_protocol.c",
    "    if (kind != GATEWAY_COMMAND_SET_ON_OFF) return GATEWAY_LINK_UNSUPPORTED_VALUE;\n    *wire = 0U;",
    "    if (kind == GATEWAY_COMMAND_SET_ON_OFF) { *wire = 0U; return GATEWAY_LINK_OK; }\n    if (kind == GATEWAY_COMMAND_SET_LEVEL) { *wire = 1U; return GATEWAY_LINK_OK; }\n    return GATEWAY_LINK_UNSUPPORTED_VALUE;",
)
replace_once(
    "main/gateway_link_protocol.c",
    "    if (wire != 0U) return GATEWAY_LINK_UNSUPPORTED_VALUE;\n    *kind = GATEWAY_COMMAND_SET_ON_OFF;\n    return GATEWAY_LINK_OK;",
    "    if (wire == 0U) { *kind = GATEWAY_COMMAND_SET_ON_OFF; return GATEWAY_LINK_OK; }\n    if (wire == 1U) { *kind = GATEWAY_COMMAND_SET_LEVEL; return GATEWAY_LINK_OK; }\n    return GATEWAY_LINK_UNSUPPORTED_VALUE;",
)

insert_before(
    "tests/host/test_gateway_link_protocol.c",
    "static void test_small_buffer_and_unknown_source_fail(void)\n",
    r'''static void test_level_command_wire(void)
{
    gateway_link_command_request_t command = {0};
    command.request_id = 99U;
    command.input.source = GATEWAY_SOURCE_ZIGBEE;
    command.input.channel = 3U;
    strcpy(command.input.id, "zigbee:00124b00aabbccdd");
    command.kind = GATEWAY_COMMAND_SET_LEVEL;
    command.value = 75.0;
    command.transition_ms = 500U;
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_command_request_payload(
        &command, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);
    gateway_link_command_request_t decoded = {0};
    assert(gateway_link_decode_command_request_payload(
        payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.kind == GATEWAY_COMMAND_SET_LEVEL);
    assert(decoded.value == 75.0);
    assert(decoded.transition_ms == 500U);
}

''',
)
replace_once(
    "tests/host/test_gateway_link_protocol.c",
    "    test_command_round_trip();\n    test_small_buffer_and_unknown_source_fail();",
    "    test_command_round_trip();\n    test_level_command_wire();\n    test_small_buffer_and_unknown_source_fail();",
)

# Runtime translates Level to ezbee MoveToLevel, intentionally not WithOnOff.
replace_once(
    "main/zigbee_gateway.c",
    "#include <ezbee/zcl/cluster/on_off.h>\n",
    "#include <ezbee/zcl/cluster/on_off.h>\n#include <ezbee/zcl/cluster/level.h>\n",
)
replace_once(
    "main/zigbee_gateway.c",
    "    const ezb_zcl_on_off_cmd_t request = {\n        .cmd_ctrl = {\n            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),\n            .dst_ep = endpoint,\n            .src_ep = GATEWAY_ENDPOINT,\n            .dis_default_rsp = false,\n            .cnf_ctx = {\n                .cb = command_confirm_callback,\n                .user_ctx = context,\n            },\n        },\n    };\n    const ezb_err_t send_result = job->command_plan.target_on ?\n        ezb_zcl_on_off_on_cmd_req(&request) : ezb_zcl_on_off_off_cmd_req(&request);",
    r'''    ezb_err_t send_result = EZB_ERR_INVALID_ARG;
    if (job->command_plan.kind == GATEWAY_COMMAND_SET_ON_OFF) {
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
        send_result = job->command_plan.target_on ?
            ezb_zcl_on_off_on_cmd_req(&request) : ezb_zcl_on_off_off_cmd_req(&request);
    } else if (job->command_plan.kind == GATEWAY_COMMAND_SET_LEVEL) {
        const ezb_zcl_level_move_to_level_cmd_t request = {
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
            .payload = {
                .level = job->command_plan.level,
                .transition_time = job->command_plan.transition_time,
                .options_mask = 0U,
                .options_override = 0U,
            },
        };
        send_result = ezb_zcl_level_move_to_level_cmd_req(&request);
    }''',
)

# Human-readable logs.
replace_once(
    "main/gateway_transport.c",
    '        "battery_percent", "mains_voltage", "voltage", "current", "power", "energy", "on_off",\n',
    '        "battery_percent", "mains_voltage", "voltage", "current", "power", "energy", "on_off", "level",\n',
)

# Docs/progress.
replace_once(
    "docs/GATEWAY_LINK_V2.md",
    "The first normalized command kind is `SET_ON_OFF`; it accepts value `0` or `1` and requires zero transition time.",
    "Normalized command kinds are `SET_ON_OFF` and `SET_LEVEL`. `SET_ON_OFF` accepts value `0` or `1` and requires zero transition time. `SET_LEVEL` accepts `0..100` percent and uses transition time in exact 100 ms increments; C6 translates it to standard ZCL `MoveToLevel` without the implicit On/Off variant.",
)
replace_once(
    "docs/CONTINUATION.md",
    "- The next implementation slice adds normalized Level Control, then the separate second-C6 emulator profiles for deterministic command/state round-trip testing.",
    "- Phase 6 adds normalized Level Control: CurrentLevel is normalized to percent and `SET_LEVEL` maps to standard `MoveToLevel` without implicit On/Off side effects.\n- The next implementation slice is the separate second-C6 emulator app/profiles for deterministic interview/reporting/command/state round-trip testing.",
)
insert_before(
    "docs/ARCHITECTURE.md",
    "## Normalized command ownership\n",
    "Level Control follows the same boundary: `CurrentLevel` becomes a normalized percent measurement/capability and `SET_LEVEL` is translated only inside C6 to standard ZCL `MoveToLevel`. The normalized API does not expose raw level bytes or ZCL transition units.\n\n",
)

print("phase6 Level Control patch prepared")
