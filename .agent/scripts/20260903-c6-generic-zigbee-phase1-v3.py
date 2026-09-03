from pathlib import Path

EXPECTED_HEAD = "2659604a5d940c13584914855e9672eab154079d"


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


replace(
    "main/gateway_inputs.c",
    """    } else {
        snprintf(input.id, sizeof(input.id), "zigbee-short:%04x", short_addr);
    }
""",
    """    } else {
        (void)short_addr;
    }
""",
)

replace(
    "tests/host/test_gateway_inputs.c",
    """static void test_zigbee_short_fallback(void)
{
    const gateway_input_id_t input = gateway_input_make_zigbee(
        NULL, false, 0x42abU, 1U);
    assert(strcmp(input.id, "zigbee-short:42ab") == 0);
}
""",
    """static void test_zigbee_requires_ieee(void)
{
    const gateway_input_id_t input = gateway_input_make_zigbee(
        NULL, false, 0x42abU, 1U);
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 1U);
    assert(input.id[0] == '\0');
}
""",
)
replace(
    "tests/host/test_gateway_inputs.c",
    "    test_zigbee_short_fallback();\n",
    "    test_zigbee_requires_ieee();\n",
)

replace(
    "main/gateway_device_state.h",
    "#define GATEWAY_INVALID_SHORT_ADDR 0xffffU\n",
    "#define GATEWAY_INVALID_SHORT_ADDR 0xffffU\n#define GATEWAY_BASIC_TEXT_MAX_BYTES 24U\n",
)
replace(
    "main/gateway_device_state.h",
    """    gateway_input_capabilities_t input_capabilities;
    bool input_announced;
""",
    """    gateway_input_capabilities_t input_capabilities;
    char manufacturer[GATEWAY_BASIC_TEXT_MAX_BYTES];
    char model[GATEWAY_BASIC_TEXT_MAX_BYTES];
    bool input_announced;
""",
)
replace(
    "main/gateway_device_state.h",
    """void gateway_device_reset_discovery(device_slot_t *slot);
endpoint_state_t *gateway_device_endpoint_state(
""",
    """void gateway_device_reset_discovery(device_slot_t *slot);
bool gateway_device_endpoint_update_basic(
    endpoint_state_t *state, const char *manufacturer, const char *model);
endpoint_state_t *gateway_device_endpoint_state(
""",
)

replace(
    "main/gateway_device_state.c",
    """void gateway_device_reset_discovery(device_slot_t *slot)
{
    if (slot != NULL) {
        slot->discovery_short_addr = GATEWAY_INVALID_SHORT_ADDR;
    }
}

endpoint_state_t *gateway_device_endpoint_state(
""",
    """void gateway_device_reset_discovery(device_slot_t *slot)
{
    if (slot != NULL) {
        slot->discovery_short_addr = GATEWAY_INVALID_SHORT_ADDR;
    }
}

static bool update_bounded_text(
    char target[GATEWAY_BASIC_TEXT_MAX_BYTES], const char *value)
{
    if (target == NULL || value == NULL) {
        return false;
    }
    char bounded[GATEWAY_BASIC_TEXT_MAX_BYTES] = {0};
    strncpy(bounded, value, sizeof(bounded) - 1U);
    if (memcmp(target, bounded, sizeof(bounded)) == 0) {
        return false;
    }
    memcpy(target, bounded, sizeof(bounded));
    return true;
}

bool gateway_device_endpoint_update_basic(
    endpoint_state_t *state, const char *manufacturer, const char *model)
{
    if (state == NULL) {
        return false;
    }
    bool changed = false;
    if (manufacturer != NULL) {
        changed = update_bounded_text(state->manufacturer, manufacturer) || changed;
    }
    if (model != NULL) {
        changed = update_bounded_text(state->model, model) || changed;
    }
    return changed;
}

endpoint_state_t *gateway_device_endpoint_state(
""",
)

replace(
    "tests/host/test_gateway_device_state.c",
    "#include <stdio.h>\n",
    "#include <stdio.h>\n#include <string.h>\n",
)
replace(
    "tests/host/test_gateway_device_state.c",
    """static const uint8_t IEEE_B[8] = {8, 7, 6, 5, 4, 3, 2, 1};

int main(void)
""",
    """static const uint8_t IEEE_B[8] = {8, 7, 6, 5, 4, 3, 2, 1};

static void test_basic_metadata(endpoint_state_t *state)
{
    assert(state != NULL);
    assert(gateway_device_endpoint_update_basic(state, "Acme", "Model A"));
    assert(strcmp(state->manufacturer, "Acme") == 0);
    assert(strcmp(state->model, "Model A") == 0);
    assert(!gateway_device_endpoint_update_basic(state, "Acme", "Model A"));
    assert(gateway_device_endpoint_update_basic(state, NULL, "Model B"));
    assert(strcmp(state->manufacturer, "Acme") == 0);
    assert(strcmp(state->model, "Model B") == 0);
    assert(gateway_device_endpoint_update_basic(
        state, "Manufacturer name longer than the bounded field", NULL));
    assert(strlen(state->manufacturer) == GATEWAY_BASIC_TEXT_MAX_BYTES - 1U);
}

int main(void)
""",
)
replace(
    "tests/host/test_gateway_device_state.c",
    "    assert(gateway_device_endpoint_state(a2, 99U, true) == NULL);\n",
    "    test_basic_metadata(gateway_device_endpoint_state(a2, 1U, false));\n    assert(gateway_device_endpoint_state(a2, 99U, true) == NULL);\n",
)

replace(
    "main/zigbee_gateway.c",
    """static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries
);
""",
    """static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries
);
static bool publish_generic_input(
    device_slot_t *slot, endpoint_state_t *state, bool available);
""",
)
replace(
    "main/zigbee_gateway.c",
    """    const gateway_input_id_t input = gateway_input_make_zigbee(
        device.ieee, device.ieee_valid, device.short_addr, header->src_ep);
""",
    """    gateway_input_id_t input = {0};
    const bool stable_input = gateway_zigbee_stable_input_id(
        device.ieee, device.ieee_valid, header->src_ep, &input);
""",
)
replace(
    "main/zigbee_gateway.c",
    """        if (gateway_zcl_normalize(
                header->cluster_id,
""",
    """        if (stable_input && gateway_zcl_normalize(
                header->cluster_id,
""",
)

p = Path("main/zigbee_gateway.c")
text = p.read_text()
start = text.index("static void publish_basic_read(")
end = text.index("static void publish_config_response(", start)
new_basic = """static void publish_basic_read(const ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT ||
        header->cluster_id != EZB_ZCL_CLUSTER_ID_BASIC) {
        return;
    }

    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = gateway_device_find_by_short(device.short_addr, false);
    endpoint_state_t *state =
        slot == NULL ? NULL : endpoint_state(slot, header->src_ep, false);
    bool seen = false;
    bool model_changed = false;
    for (ezb_zcl_read_attr_rsp_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        if (item->attr_id != ZCL_ATTR_BASIC_MANUFACTURER_NAME &&
            item->attr_id != ZCL_ATTR_BASIC_MODEL_IDENTIFIER) {
            continue;
        }
        seen = true;
        if (item->status != EZB_ZCL_STATUS_SUCCESS ||
            item->attr_type != EZB_ZCL_ATTR_TYPE_STRING ||
            item->attr_value == NULL) {
            continue;
        }
        const uint8_t *string = item->attr_value;
        if (string[0] == 0xffU) {
            continue;
        }
        gateway_event_t event = gateway_event_make(GATEWAY_EVENT_BASIC, &device);
        event.endpoint = header->src_ep;
        strncpy(
            event.data.text.key,
            item->attr_id == ZCL_ATTR_BASIC_MANUFACTURER_NAME ?
                "manufacturer" : "model",
            sizeof(event.data.text.key) - 1U
        );
        const size_t len = string[0] < GATEWAY_TEXT_MAX_BYTES - 1U ?
            string[0] : GATEWAY_TEXT_MAX_BYTES - 1U;
        memcpy(event.data.text.value, string + 1, len);
        event.data.text.value[len] = '\0';
        if (state != NULL) {
            const bool changed = item->attr_id == ZCL_ATTR_BASIC_MANUFACTURER_NAME ?
                gateway_device_endpoint_update_basic(
                    state, event.data.text.value, NULL) :
                gateway_device_endpoint_update_basic(
                    state, NULL, event.data.text.value);
            if (item->attr_id == ZCL_ATTR_BASIC_MODEL_IDENTIFIER && changed) {
                model_changed = true;
            }
        }
        gateway_event_publish(&event);
    }

    if (seen && state != NULL) {
        state->basic_state = BASIC_COMPLETE;
    }
    if (model_changed && state != NULL && state->input_announced) {
        (void)publish_generic_input(slot, state, true);
    }
}

"""
p.write_text(text[:start] + new_basic + text[end:])

replace(
    "main/zigbee_gateway.c",
    """    event.endpoint = state->endpoint;
    event.data.input_desc.capabilities = state->input_capabilities;
    if (!gateway_event_publish(&event)) {
""",
    """    event.endpoint = state->endpoint;
    event.data.input_desc.capabilities = state->input_capabilities;
    strncpy(
        event.data.input_desc.model, state->model,
        sizeof(event.data.input_desc.model) - 1U);
    if (!gateway_event_publish(&event)) {
""",
)

replace(
    "docs/ARCHITECTURE.md",
    "Stable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are only a provisional fallback when IEEE recovery has not completed. The SCD4x adapter uses the sensor 48-bit serial number and channel 0, with `scd4x:0x62` only as a fallback when the serial cannot be read.\n",
    "Stable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are routing-only and are never emitted as normalized input identities. The SCD4x adapter uses the sensor 48-bit serial number and channel 0, with `scd4x:0x62` only as a fallback when the serial cannot be read.\n",
)

replace(
    "docs/CONTINUATION.md",
    """Execution order:

1. audit the current coordinator against the Generic Zigbee Device Interview & Capability Discovery goal below;
""",
    """Implementation progress after the generic-coordinator audit:

- Phase 1 hardens the normalized Zigbee boundary so IEEE+endpoint is the only external Zigbee input identity, preserves Basic manufacturer/model metadata in endpoint state, and refreshes the normalized input descriptor when model metadata becomes known.
- The next implementation slice is the richer standard-ZCL capability/reporting state needed before frontend-driven Configure Reporting and writable commands.

Execution order:

1. audit the current coordinator against the Generic Zigbee Device Interview & Capability Discovery goal below;
""",
)
