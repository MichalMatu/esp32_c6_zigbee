from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_between(path: str, start_marker: str, end_marker: str, new_text: str) -> None:
    p = Path(path)
    text = p.read_text()
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    p.write_text(text[:start] + new_text + text[end:])


replace(
    "main/gateway_device_state.h",
    "#define GATEWAY_BASIC_TEXT_MAX_BYTES 24U\n",
    """#define GATEWAY_BASIC_TEXT_MAX_BYTES 24U
#define GATEWAY_MAX_BINDING_STATES_PER_DEVICE 16U
#define GATEWAY_MAX_REPORTING_STATES_PER_DEVICE 16U
#define GATEWAY_CONFIG_STATUS_UNKNOWN 0xffU
""",
)
replace(
    "main/gateway_device_state.h",
    """typedef enum { SLOT_EMPTY, SLOT_ACTIVE, SLOT_REJOIN_PENDING, SLOT_LEAVING } slot_state_t;
typedef enum { BASIC_NOT_SCHEDULED, BASIC_SCHEDULED, BASIC_COMPLETE } basic_state_t;

typedef struct {
""",
    """typedef enum { SLOT_EMPTY, SLOT_ACTIVE, SLOT_REJOIN_PENDING, SLOT_LEAVING } slot_state_t;
typedef enum { BASIC_NOT_SCHEDULED, BASIC_SCHEDULED, BASIC_COMPLETE } basic_state_t;

typedef struct {
    bool in_use;
    uint8_t endpoint;
    uint16_t cluster_id;
    bool requested;
    bool configured;
    uint8_t last_status;
    uint32_t requested_at_ms;
} binding_state_t;

typedef struct {
    bool in_use;
    uint8_t endpoint;
    uint16_t cluster_id;
    uint16_t attribute_id;
    bool requested;
    bool configured;
    uint8_t last_status;
    uint32_t requested_at_ms;
} reporting_state_t;

typedef struct {
""",
)
replace(
    "main/gateway_device_state.h",
    """    basic_state_t basic_state;
    uint32_t basic_scheduled_at_ms;
    uint8_t reporting_requested;
    uint8_t reporting_configured;
    uint32_t reporting_requested_at_ms;
    uint8_t binding_requested;
    uint8_t binding_configured;
    uint32_t binding_requested_at_ms;
    gateway_input_capabilities_t input_capabilities;
""",
    """    basic_state_t basic_state;
    uint32_t basic_scheduled_at_ms;
    gateway_input_capabilities_t input_capabilities;
""",
)
replace(
    "main/gateway_device_state.h",
    """    uint8_t pending_jobs;
    uint8_t pending_requests;
    endpoint_state_t endpoints[GATEWAY_MAX_ENDPOINTS_PER_DEVICE];
""",
    """    uint8_t pending_jobs;
    uint8_t pending_requests;
    binding_state_t bindings[GATEWAY_MAX_BINDING_STATES_PER_DEVICE];
    reporting_state_t reporting[GATEWAY_MAX_REPORTING_STATES_PER_DEVICE];
    endpoint_state_t endpoints[GATEWAY_MAX_ENDPOINTS_PER_DEVICE];
""",
)
replace(
    "main/gateway_device_state.h",
    """bool gateway_device_endpoint_update_basic(
    endpoint_state_t *state, const char *manufacturer, const char *model);
endpoint_state_t *gateway_device_endpoint_state(
""",
    """bool gateway_device_endpoint_update_basic(
    endpoint_state_t *state, const char *manufacturer, const char *model);
binding_state_t *gateway_device_binding_state(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster_id, bool create);
reporting_state_t *gateway_device_reporting_state(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster_id,
    uint16_t attribute_id, bool create);
endpoint_state_t *gateway_device_endpoint_state(
""",
)

replace(
    "main/gateway_device_state.c",
    """        memset(slot->endpoints, 0, sizeof(slot->endpoints));
        memset(&slot->device, 0, sizeof(slot->device));
""",
    """        memset(slot->bindings, 0, sizeof(slot->bindings));
        memset(slot->reporting, 0, sizeof(slot->reporting));
        memset(slot->endpoints, 0, sizeof(slot->endpoints));
        memset(&slot->device, 0, sizeof(slot->device));
""",
)
replace(
    "main/gateway_device_state.c",
    """bool gateway_device_endpoint_update_basic(
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
    """bool gateway_device_endpoint_update_basic(
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

binding_state_t *gateway_device_binding_state(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster_id, bool create)
{
    if (slot == NULL) {
        return NULL;
    }
    binding_state_t *free_state = NULL;
    for (size_t i = 0; i < GATEWAY_MAX_BINDING_STATES_PER_DEVICE; ++i) {
        binding_state_t *state = &slot->bindings[i];
        if (state->in_use && state->endpoint == endpoint &&
            state->cluster_id == cluster_id) {
            return state;
        }
        if (!state->in_use && free_state == NULL) {
            free_state = state;
        }
    }
    if (!create || free_state == NULL) {
        return NULL;
    }
    *free_state = (binding_state_t){
        .in_use = true,
        .endpoint = endpoint,
        .cluster_id = cluster_id,
        .last_status = GATEWAY_CONFIG_STATUS_UNKNOWN,
    };
    return free_state;
}

reporting_state_t *gateway_device_reporting_state(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster_id,
    uint16_t attribute_id, bool create)
{
    if (slot == NULL) {
        return NULL;
    }
    reporting_state_t *free_state = NULL;
    for (size_t i = 0; i < GATEWAY_MAX_REPORTING_STATES_PER_DEVICE; ++i) {
        reporting_state_t *state = &slot->reporting[i];
        if (state->in_use && state->endpoint == endpoint &&
            state->cluster_id == cluster_id && state->attribute_id == attribute_id) {
            return state;
        }
        if (!state->in_use && free_state == NULL) {
            free_state = state;
        }
    }
    if (!create || free_state == NULL) {
        return NULL;
    }
    *free_state = (reporting_state_t){
        .in_use = true,
        .endpoint = endpoint,
        .cluster_id = cluster_id,
        .attribute_id = attribute_id,
        .last_status = GATEWAY_CONFIG_STATUS_UNKNOWN,
    };
    return free_state;
}

endpoint_state_t *gateway_device_endpoint_state(
""",
)

replace(
    "main/gateway_reporting_policy.h",
    """uint8_t gateway_reporting_policy_reporting_mask(uint16_t cluster_id);
uint8_t gateway_reporting_policy_binding_mask(uint16_t cluster_id);
bool gateway_reporting_policy_spec(
""",
    """bool gateway_reporting_policy_requires_binding(uint16_t cluster_id);
bool gateway_reporting_policy_spec(
""",
)

p = Path("main/gateway_reporting_policy.c")
text = p.read_text()
text = text.replace("#define REPORTING_TEMPERATURE 0x01U\n#define REPORTING_HUMIDITY 0x02U\n#define REPORTING_BATTERY_PERCENT 0x04U\n#define BINDING_POLL_CONTROL 0x08U\n\n", "")
text = text.replace("    uint8_t state_mask;\n", "")
text = text.replace("        .state_mask = REPORTING_TEMPERATURE,\n", "")
text = text.replace("        .state_mask = REPORTING_HUMIDITY,\n", "")
text = text.replace("        .state_mask = REPORTING_BATTERY_PERCENT,\n", "")
start = text.index("uint8_t gateway_reporting_policy_reporting_mask(")
end = text.index("bool gateway_reporting_policy_spec(", start)
replacement = """bool gateway_reporting_policy_requires_binding(uint16_t cluster_id)
{
    return find_policy(cluster_id) != NULL || cluster_id == ZCL_CLUSTER_POLL_CONTROL;
}

"""
p.write_text(text[:start] + replacement + text[end:])

replace_between(
    "main/zigbee_gateway.c",
    "static bool schedule_binding(\n",
    "static bool schedule_reporting(\n",
    """static bool schedule_binding(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)
{
    if (!gateway_reporting_policy_requires_binding(cluster)) {
        return false;
    }
    endpoint_state_t *state = endpoint_state(slot, endpoint, true);
    if (state == NULL) {
        return false;
    }
    binding_state_t *binding = gateway_device_binding_state(
        slot, endpoint, cluster, true);
    if (binding == NULL) {
        gateway_event_warning(&slot->device, "binding state capacity exhausted");
        return false;
    }
    if (binding->requested || binding->configured) {
        return false;
    }
    if (!queue_job(DISCOVERY_BIND_CLUSTER, slot, endpoint, cluster, 0U)) {
        gateway_event_warning(&slot->device, "binding queue full");
        return false;
    }
    binding->requested = true;
    binding->requested_at_ms = gateway_uptime_ms();
    binding->last_status = GATEWAY_CONFIG_STATUS_UNKNOWN;
    return true;
}

""",
)
replace_between(
    "main/zigbee_gateway.c",
    "static bool schedule_reporting(\n",
    "static async_context_t *context_alloc(\n",
    """static bool schedule_reporting(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)
{
    gateway_reporting_spec_t spec;
    if (!gateway_reporting_policy_spec(cluster, &spec)) {
        return false;
    }
    endpoint_state_t *state = endpoint_state(slot, endpoint, true);
    if (state == NULL) {
        return false;
    }
    if (gateway_reporting_policy_requires_binding(cluster)) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, endpoint, cluster, false);
        if (binding == NULL || !binding->configured) {
            return false;
        }
    }
    reporting_state_t *reporting = gateway_device_reporting_state(
        slot, endpoint, cluster, spec.attribute_id, true);
    if (reporting == NULL) {
        gateway_event_warning(&slot->device, "reporting state capacity exhausted");
        return false;
    }
    if (reporting->requested || reporting->configured) {
        return false;
    }
    if (!queue_job(DISCOVERY_CONFIG_REPORTING, slot, endpoint, cluster, 0U)) {
        gateway_event_warning(&slot->device, "reporting queue full");
        return false;
    }
    reporting->requested = true;
    reporting->requested_at_ms = gateway_uptime_ms();
    reporting->last_status = GATEWAY_CONFIG_STATUS_UNKNOWN;
    return true;
}

""",
)
replace_between(
    "main/zigbee_gateway.c",
    "static void clear_pending(\n",
    "static void retry_or_fail(\n",
    """static void clear_pending(device_slot_t *slot, const discovery_job_t *job)
{
    endpoint_state_t *state = endpoint_state(slot, job->endpoint, false);
    if (job->kind == DISCOVERY_READ_BASIC && state != NULL &&
        state->basic_state == BASIC_SCHEDULED) {
        state->basic_state = BASIC_NOT_SCHEDULED;
    }
    if (job->kind == DISCOVERY_BIND_CLUSTER) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, job->endpoint, job->cluster_id, false);
        if (binding != NULL) {
            binding->requested = false;
        }
    }
    if (job->kind == DISCOVERY_CONFIG_REPORTING) {
        gateway_reporting_spec_t spec;
        if (gateway_reporting_policy_spec(job->cluster_id, &spec)) {
            reporting_state_t *reporting = gateway_device_reporting_state(
                slot, job->endpoint, job->cluster_id, spec.attribute_id, false);
            if (reporting != NULL) {
                reporting->requested = false;
            }
        }
    }
}

""",
)
replace_between(
    "main/zigbee_gateway.c",
    "static void publish_config_response(\n",
    "static void handle_check_in(\n",
    """static void publish_config_response(
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
        if (reporting != NULL) {
            reporting->requested = false;
            reporting->last_status = item->status;
            reporting->configured = item->status == EZB_ZCL_STATUS_SUCCESS;
        }
        gateway_event_t event = gateway_event_make(
            GATEWAY_EVENT_REPORTING_CONFIG, &device
        );
        event.endpoint = header->src_ep;
        event.data.reporting.cluster_id = header->cluster_id;
        event.data.reporting.attribute_id = item->attr_id;
        event.data.reporting.status = item->status;
        gateway_event_publish(&event);
    }
}

""",
)
replace_between(
    "main/zigbee_gateway.c",
    "static void handle_check_in(\n",
    "static void zcl_core_action_handler(\n",
    """static void handle_check_in(ezb_zcl_poll_control_check_in_message_t *message)
{
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    message->out.start_fast_poll = true;
    message->out.fast_poll_timeout = GATEWAY_FAST_POLL_TIMEOUT_QUARTER_SECONDS;

    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT ||
        header->src_addr.addr_mode != EZB_ADDR_MODE_SHORT) {
        return;
    }
    device_slot_t *slot = gateway_device_upsert(header->src_addr.u.short_addr, NULL);
    if (slot == NULL) {
        return;
    }

    const uint32_t now_ms = gateway_uptime_ms();
    for (size_t i = 0; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
        if (!slot->endpoints[i].in_use) {
            continue;
        }
        endpoint_state_t *state = &slot->endpoints[i];
        if (state->basic_state == BASIC_SCHEDULED &&
            (uint32_t)(now_ms - state->basic_scheduled_at_ms) >=
                GATEWAY_REQUEST_STALE_MS) {
            state->basic_state = BASIC_NOT_SCHEDULED;
        }
    }
    for (size_t i = 0; i < GATEWAY_MAX_BINDING_STATES_PER_DEVICE; ++i) {
        binding_state_t *binding = &slot->bindings[i];
        if (binding->in_use && binding->requested &&
            (uint32_t)(now_ms - binding->requested_at_ms) >=
                GATEWAY_REQUEST_STALE_MS) {
            binding->requested = false;
        }
    }
    for (size_t i = 0; i < GATEWAY_MAX_REPORTING_STATES_PER_DEVICE; ++i) {
        reporting_state_t *reporting = &slot->reporting[i];
        if (reporting->in_use && reporting->requested &&
            (uint32_t)(now_ms - reporting->requested_at_ms) >=
                GATEWAY_REQUEST_STALE_MS) {
            reporting->requested = false;
        }
    }

    gateway_event_t event = gateway_event_make(
        GATEWAY_EVENT_DEVICE_CHECK_IN, &slot->device
    );
    event.endpoint = header->src_ep;
    gateway_event_publish(&event);
    if (!queue_job(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U, 0U, 0U)) {
        gateway_event_warning(&slot->device, "check-in discovery queue full");
    }
}

""",
)
replace_between(
    "main/zigbee_gateway.c",
    "static void binding_callback(\n",
    "static bool publish_generic_input(\n",
    """static void binding_callback(const ezb_zdp_bind_req_result_t *result, void *user_ctx)
{
    async_context_t *context = user_ctx;
    device_slot_t *slot = NULL;
    const uint8_t status =
        result != NULL && result->error == EZB_ERR_NONE && result->rsp != NULL ?
        result->rsp->status : 0xffU;

    if (context != NULL && context->in_use && context_route(context, &slot)) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, context->endpoint, context->cluster_id, false);
        if (binding != NULL) {
            binding->last_status = status;
        }
        if (status == EZB_ZDP_STATUS_SUCCESS && binding != NULL) {
            binding->requested = false;
            binding->configured = true;
            schedule_reporting(slot, context->endpoint, context->cluster_id);
        } else if (binding != NULL) {
            retry_or_fail(
                (discovery_job_t){
                    .kind = DISCOVERY_BIND_CLUSTER,
                    .device = context->device,
                    .route_short_addr = context->route_short_addr,
                    .endpoint = context->endpoint,
                    .cluster_id = context->cluster_id,
                    .retry_count = context->retry_count,
                },
                "binding failed after retries"
            );
        }
        gateway_event_t event = gateway_event_make(
            GATEWAY_EVENT_BINDING, &slot->device
        );
        event.endpoint = context->endpoint;
        event.data.binding.cluster_id = context->cluster_id;
        event.data.binding.status = status;
        gateway_event_publish(&event);
    }
    context_release(context);
}

""",
)
replace(
    "main/zigbee_gateway.c",
    """        if (gateway_reporting_policy_binding_mask(cluster) != 0U) {
            schedule_binding(slot, desc->ep_id, cluster);
        } else if (gateway_reporting_policy_reporting_mask(cluster) != 0U) {
            schedule_reporting(slot, desc->ep_id, cluster);
        }
""",
    """        if (gateway_reporting_policy_requires_binding(cluster)) {
            schedule_binding(slot, desc->ep_id, cluster);
        } else {
            gateway_reporting_spec_t spec;
            if (gateway_reporting_policy_spec(cluster, &spec)) {
                schedule_reporting(slot, desc->ep_id, cluster);
            }
        }
""",
)

zigbee = Path("main/zigbee_gateway.c").read_text()
for forbidden in (
    "gateway_reporting_policy_reporting_mask",
    "gateway_reporting_policy_binding_mask",
    "reporting_requested",
    "reporting_configured",
    "binding_requested",
    "binding_configured",
):
    if forbidden in zigbee:
        raise SystemExit(f"stale bitmap reporting state reference remains: {forbidden}")

replace(
    "tests/host/test_gateway_reporting_policy.c",
    """    assert(gateway_reporting_policy_reporting_mask(CLUSTER_TEMPERATURE) != 0U);
    assert(gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, &spec));
""",
    """    assert(gateway_reporting_policy_requires_binding(CLUSTER_TEMPERATURE));
    assert(gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, &spec));
""",
)
replace(
    "tests/host/test_gateway_reporting_policy.c",
    """    assert(gateway_reporting_policy_reporting_mask(CLUSTER_POLL_CONTROL) == 0U);
    assert(gateway_reporting_policy_binding_mask(CLUSTER_POLL_CONTROL) != 0U);
    assert(!gateway_reporting_policy_spec(CLUSTER_POLL_CONTROL, &spec));
    assert(gateway_reporting_policy_reporting_mask(0xffffU) == 0U);
    assert(gateway_reporting_policy_binding_mask(0xffffU) == 0U);
""",
    """    assert(gateway_reporting_policy_requires_binding(CLUSTER_HUMIDITY));
    assert(gateway_reporting_policy_requires_binding(CLUSTER_POWER_CONFIG));
    assert(gateway_reporting_policy_requires_binding(CLUSTER_POLL_CONTROL));
    assert(!gateway_reporting_policy_spec(CLUSTER_POLL_CONTROL, &spec));
    assert(!gateway_reporting_policy_requires_binding(0xffffU));
""",
)

replace(
    "tests/host/test_gateway_device_state.c",
    """static void test_basic_metadata(endpoint_state_t *state)
{
""",
    """static void test_config_state(device_slot_t *slot)
{
    binding_state_t *first_binding = NULL;
    for (uint16_t i = 0; i < GATEWAY_MAX_BINDING_STATES_PER_DEVICE; ++i) {
        binding_state_t *state = gateway_device_binding_state(
            slot, 1U, (uint16_t)(0x1000U + i), true);
        assert(state != NULL);
        assert(state->last_status == GATEWAY_CONFIG_STATUS_UNKNOWN);
        if (i == 0U) {
            first_binding = state;
        }
    }
    assert(gateway_device_binding_state(slot, 1U, 0x1000U, false) == first_binding);
    assert(gateway_device_binding_state(slot, 1U, 0x2000U, true) == NULL);

    reporting_state_t *first_reporting = NULL;
    for (uint16_t i = 0; i < GATEWAY_MAX_REPORTING_STATES_PER_DEVICE; ++i) {
        reporting_state_t *state = gateway_device_reporting_state(
            slot, 1U, 0x3000U, i, true);
        assert(state != NULL);
        assert(state->last_status == GATEWAY_CONFIG_STATUS_UNKNOWN);
        if (i == 0U) {
            first_reporting = state;
        }
    }
    assert(gateway_device_reporting_state(
        slot, 1U, 0x3000U, 0U, false) == first_reporting);
    assert(gateway_device_reporting_state(
        slot, 1U, 0x3000U, 0x4000U, true) == NULL);
}

static void test_basic_metadata(endpoint_state_t *state)
{
""",
)
replace(
    "tests/host/test_gateway_device_state.c",
    """    test_basic_metadata(gateway_device_endpoint_state(a2, 1U, false));
    assert(gateway_device_endpoint_state(a2, 99U, true) == NULL);
""",
    """    test_basic_metadata(gateway_device_endpoint_state(a2, 1U, false));
    test_config_state(a2);
    assert(gateway_device_endpoint_state(a2, 99U, true) == NULL);
""",
)

replace(
    "docs/ARCHITECTURE.md",
    "`gateway_reporting_policy.c/.h` is the pure, host-testable table for standard binding/reporting masks and Configure Reporting parameters.",
    "`gateway_reporting_policy.c/.h` is the pure, host-testable table for standard binding requirements and per-attribute Configure Reporting parameters.",
)
replace(
    "docs/ARCHITECTURE.md",
    "`gateway_device_state.c/.h` owns the bounded IEEE-first device/endpoint registry, generation-safe references, short-address replacement, and reclaim rules. It has no ESP Zigbee or FreeRTOS dependency.",
    "`gateway_device_state.c/.h` owns the bounded IEEE-first device/endpoint registry, generation-safe references, short-address replacement, reclaim rules, and bounded per-device binding/reporting request records keyed by endpoint/cluster/attribute. It has no ESP Zigbee or FreeRTOS dependency.",
)
replace(
    "docs/CONTINUATION.md",
    """Implementation progress after the generic-coordinator audit:

- Phase 1 hardens the normalized Zigbee boundary so IEEE+endpoint is the only external Zigbee input identity, preserves Basic manufacturer/model metadata in endpoint state, and refreshes the normalized input descriptor when model metadata becomes known.
- The next implementation slice is the richer standard-ZCL capability/reporting state needed before frontend-driven Configure Reporting and writable commands.
""",
    """Implementation progress after the generic-coordinator audit:

- Phase 1 is complete: IEEE+endpoint is the only normalized Zigbee input identity, Basic manufacturer/model metadata is retained, and input descriptors refresh when model metadata becomes known.
- Phase 2 replaces the fixed reporting/binding bitmaps with bounded records keyed by endpoint/cluster/attribute so Configure Reporting state can scale beyond one hard-coded bit per supported cluster and preserve per-attribute result status.
- The next implementation slice is normalized capability/configuration exposure through GatewayLink, followed by frontend-driven Configure Reporting and writable commands.
""",
)
